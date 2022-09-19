use crate::ciphertext::Ciphertext;
use crate::{ClientKey, PLAINTEXT_TRUE};
use concrete_core::prelude::*;
use serde::{Deserialize, Deserializer, Serialize, Serializer};
use std::error::Error;

use super::{BooleanServerKey, Bootstrapper};

/// A structure containing the server public key.
///
/// This server key data lives on the CPU.
///
/// The server key is generated by the client and is meant to be published: the client
/// sends it to the server so it can compute homomorphic Boolean circuits.
///
/// In more details, it contains:
/// * `bootstrapping_key` - a public key, used to perform the bootstrapping operation.
/// * `key_switching_key` - a public key, used to perform the key-switching operation.
#[derive(Clone)]
pub(crate) struct CpuServerKey {
    pub(super) bootstrapping_key: FftwFourierLweBootstrapKey32,
    pub(super) key_switching_key: LweKeyswitchKey32,
}

impl CpuServerKey {
    pub(crate) fn new(engine: &mut DefaultEngine, cks: &ClientKey) -> Result<Self, Box<dyn Error>> {
        // convert into a variance for rlwe context
        let var_rlwe = Variance(cks.parameters.glwe_modular_std_dev.get_variance());
        // creation of the bootstrapping key in the Fourier domain
        let mut fftw_engine = FftwEngine::new(()).unwrap();

        let bootstrap_key: LweBootstrapKey32 = engine.generate_new_lwe_bootstrap_key(
            &cks.lwe_secret_key,
            &cks.glwe_secret_key,
            cks.parameters.pbs_base_log,
            cks.parameters.pbs_level,
            var_rlwe,
        )?;

        let fourier_bsk = fftw_engine.convert_lwe_bootstrap_key(&bootstrap_key)?;

        // Convert the GLWE secret key into an LWE secret key:
        let big_lwe_secret_key =
            engine.transform_glwe_secret_key_to_lwe_secret_key(cks.glwe_secret_key.clone())?;

        // convert into a variance for lwe context
        let var_lwe = Variance(cks.parameters.lwe_modular_std_dev.get_variance());
        // creation of the key switching key
        let ksk = engine.generate_new_lwe_keyswitch_key(
            &big_lwe_secret_key,
            &cks.lwe_secret_key,
            cks.parameters.ks_level,
            cks.parameters.ks_base_log,
            var_lwe,
        )?;

        Ok(Self {
            bootstrapping_key: fourier_bsk,
            key_switching_key: ksk,
        })
    }
}

impl BooleanServerKey for CpuServerKey {
    fn lwe_size(&self) -> LweSize {
        self.bootstrapping_key.input_lwe_dimension().to_lwe_size()
    }
}

/// Memory used as buffer for the bootstrap
///
/// It contains contiguous chunk which is then sliced and converted
/// into core's View types.
#[derive(Default)]
struct Memory {
    elements: Vec<u32>,
}

impl Memory {
    /// Returns a tuple with buffers that matches the server key.
    ///
    /// - The first element is the accumulator for bootstrap step.
    /// - The second element is a lwe buffer where the result of the of the bootstrap should be
    ///   written
    fn as_buffers(
        &mut self,
        engine: &mut DefaultEngine,
        server_key: &CpuServerKey,
    ) -> (GlweCiphertextView32, LweCiphertextMutView32) {
        let num_elem_in_accumulator = server_key
            .bootstrapping_key
            .glwe_dimension()
            .to_glwe_size()
            .0
            * server_key.bootstrapping_key.polynomial_size().0;
        let num_elem_in_lwe = server_key
            .bootstrapping_key
            .output_lwe_dimension()
            .to_lwe_size()
            .0;
        let total_elem_needed = num_elem_in_accumulator + num_elem_in_lwe;

        let all_elements = if self.elements.len() < total_elem_needed {
            self.elements.resize(total_elem_needed, 0u32);
            self.elements.as_mut_slice()
        } else {
            &mut self.elements[..total_elem_needed]
        };

        let (accumulator_elements, lwe_elements) =
            all_elements.split_at_mut(num_elem_in_accumulator);
        accumulator_elements
            [num_elem_in_accumulator - server_key.bootstrapping_key.polynomial_size().0..]
            .fill(PLAINTEXT_TRUE);

        let accumulator = engine
            .create_glwe_ciphertext_from(
                &*accumulator_elements,
                server_key.bootstrapping_key.polynomial_size(),
            )
            .unwrap();
        let lwe = engine.create_lwe_ciphertext_from(lwe_elements).unwrap();

        (accumulator, lwe)
    }
}

/// Performs ciphertext bootstraps on the CPU
pub(crate) struct CpuBootstrapper {
    memory: Memory,
    engine: DefaultEngine,
    fftw_engine: FftwEngine,
}

impl Default for CpuBootstrapper {
    fn default() -> Self {
        let engine = DefaultEngine::new(Box::new(UnixSeeder::new(0)))
            .expect("Unexpectedly failed to create a core engine");

        let fftw_engine = FftwEngine::new(()).unwrap();
        Self {
            memory: Default::default(),
            engine,
            fftw_engine,
        }
    }
}

impl Bootstrapper for CpuBootstrapper {
    type ServerKey = CpuServerKey;

    fn bootstrap(
        &mut self,
        input: &LweCiphertext32,
        server_key: &Self::ServerKey,
    ) -> Result<LweCiphertext32, Box<dyn Error>> {
        let (accumulator, mut buffer_lwe_after_pbs) =
            self.memory.as_buffers(&mut self.engine, server_key);

        // Need to be able to get view from &Lwe
        let inner_data = self
            .engine
            .consume_retrieve_lwe_ciphertext(input.clone())
            .unwrap();
        let input = self
            .engine
            .create_lwe_ciphertext_from(inner_data.as_slice())
            .unwrap();

        self.fftw_engine.discard_bootstrap_lwe_ciphertext(
            &mut buffer_lwe_after_pbs,
            &input,
            &accumulator,
            &server_key.bootstrapping_key,
        )?;

        let data = self
            .engine
            .consume_retrieve_lwe_ciphertext(buffer_lwe_after_pbs)
            .unwrap()
            .to_vec();
        let ct = self.engine.create_lwe_ciphertext_from(data)?;
        Ok(ct)
    }

    fn keyswitch(
        &mut self,
        input: &LweCiphertext32,
        server_key: &Self::ServerKey,
    ) -> Result<LweCiphertext32, Box<dyn Error>> {
        // Allocate the output of the KS
        let mut ct_ks = self
            .engine
            .create_lwe_ciphertext_from(vec![0u32; server_key.lwe_size().0])?;

        self.engine.discard_keyswitch_lwe_ciphertext(
            &mut ct_ks,
            input,
            &server_key.key_switching_key,
        )?;

        Ok(ct_ks)
    }

    fn bootstrap_keyswitch(
        &mut self,
        ciphertext: LweCiphertext32,
        server_key: &Self::ServerKey,
    ) -> Result<Ciphertext, Box<dyn Error>> {
        let (accumulator, mut buffer_lwe_after_pbs) =
            self.memory.as_buffers(&mut self.engine, server_key);

        let mut inner_data = self
            .engine
            .consume_retrieve_lwe_ciphertext(ciphertext)
            .unwrap();
        let input_view = self
            .engine
            .create_lwe_ciphertext_from(inner_data.as_slice())?;

        self.fftw_engine.discard_bootstrap_lwe_ciphertext(
            &mut buffer_lwe_after_pbs,
            &input_view,
            &accumulator,
            &server_key.bootstrapping_key,
        )?;

        // Convert from a mut view to a view
        let slice = self
            .engine
            .consume_retrieve_lwe_ciphertext(buffer_lwe_after_pbs)
            .unwrap();
        let buffer_lwe_after_pbs = self.engine.create_lwe_ciphertext_from(&*slice)?;

        let mut output_view = self
            .engine
            .create_lwe_ciphertext_from(inner_data.as_mut_slice())?;

        // Compute a key switch to get back to input key
        self.engine.discard_keyswitch_lwe_ciphertext(
            &mut output_view,
            &buffer_lwe_after_pbs,
            &server_key.key_switching_key,
        )?;

        let ciphertext = self.engine.create_lwe_ciphertext_from(inner_data)?;

        Ok(Ciphertext::Encrypted(ciphertext))
    }
}

#[derive(Serialize, Deserialize)]
struct SerializableCpuServerKey {
    pub key_switching_key: Vec<u8>,
    pub bootstrapping_key: Vec<u8>,
}

impl Serialize for CpuServerKey {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        let mut ser_eng = DefaultSerializationEngine::new(()).map_err(serde::ser::Error::custom)?;
        let mut fftw_ser_eng =
            FftwSerializationEngine::new(()).map_err(serde::ser::Error::custom)?;

        let key_switching_key = ser_eng
            .serialize(&self.key_switching_key)
            .map_err(serde::ser::Error::custom)?;
        let bootstrapping_key = fftw_ser_eng
            .serialize(&self.bootstrapping_key)
            .map_err(serde::ser::Error::custom)?;

        SerializableCpuServerKey {
            key_switching_key,
            bootstrapping_key,
        }
        .serialize(serializer)
    }
}

impl<'de> Deserialize<'de> for CpuServerKey {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let thing = SerializableCpuServerKey::deserialize(deserializer)
            .map_err(serde::de::Error::custom)?;
        let mut ser_eng = DefaultSerializationEngine::new(()).map_err(serde::de::Error::custom)?;
        let mut fftw_ser_eng =
            FftwSerializationEngine::new(()).map_err(serde::de::Error::custom)?;

        Ok(Self {
            key_switching_key: ser_eng
                .deserialize(thing.key_switching_key.as_slice())
                .map_err(serde::de::Error::custom)?,
            bootstrapping_key: fftw_ser_eng
                .deserialize(thing.bootstrapping_key.as_slice())
                .map_err(serde::de::Error::custom)?,
        })
    }
}
