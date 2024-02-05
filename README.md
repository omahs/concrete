<p align="center">
<!-- product name logo -->
  <img width=600 src="https://user-images.githubusercontent.com/5758427/231207493-62676aea-4cb9-4bb4-92b0-20309c8a933a.png">
</p>
<hr/>

<p align="center">
  <a href="https://docs.zama.ai/concrete"> 📒 Read documentation</a> | <a href="https://zama.ai/community"> 💛 Community support</a> | <a href="https://github.com/zama-ai/awesome-zama"> 📚 FHE resources</a>
</p>
<p align="center">
<!-- Version badge using shields.io -->
  <a href="https://github.com/zama-ai/concrete/releases">
    <img src="https://img.shields.io/github/v/release/zama-ai/concrete?style=flat-square">
  </a>
  <!-- Link to tutorials badge using shields.io -->
  <a href="#license">
    <img src="https://img.shields.io/badge/License-BSD--3--Clause--Clear-orange?style=flat-square">
  </a>
<!-- Zama Bounty Program -->
  <a href="https://github.com/zama-ai/bounty-program">
    <img src="https://img.shields.io/badge/Contribute-Zama%20Bounty%20Program-yellow?style=flat-square">
  </a>
</p>



## Table of Contents
- **[About](#about)**
  - [What is Concrete](#-what-is-concrete)
  - [Main features](#-main-features)
- **[Getting Started](#getting-started)**
   - [Installation](#-installation)
   - [A simple example](#-a-simple-example)
- **[Resources](#resources)**
   - [Tutorials](#-tutorials)
   - [Documentation](#-documentation)
- **[Working with Concrete](working-with-concrete)**
   - [Citations](#-citations)
   - [Contributing](#-contributing)
   - [License](#-license)
- **[Support](#support)**
<br></br>


## About

### 🟨 What is Concrete
Concrete is an open-source FHE Compiler which simplifies the use of fully homomorphic encryption (FHE).

FHE is a powerful cryptographic tool, which allows computation to be performed directly on encrypted data without needing to decrypt it first. With FHE, you can build services that preserve privacy for all users. FHE is also great against data breaches as everything is done on encrypted data. Even if the server is compromised, in the end no sensitive data is leaked.

Since writing FHE programs can be difficult, Concrete, based on LLVM, make this process easier for developers. Concrete is a generic library that supports a variety of use cases. If you have a specific use case, or a specific field of computation, you may want to build abstractions on top of Concrete. One such example is [Concrete ML](https://github.com/zama-ai/concrete-ml), which is built on top of Concrete to simplify Machine Learning oriented use cases.
<br></br>

### 🟨 Main features
Concrete features include:
- Ability to compile Python functions (that may include NumPy) to their FHE equivalents, to operate on encrypted data
- Support for [large collection of operators](https://docs.zama.ai/concrete/getting-started/compatibility)
- Partial support for floating points
- Support for table lookups on integers
- Support for integration with Client / Server architectures

*Learn more features in Concrete's [documentation](https://docs.zama.ai/concrete/readme).*
<br></br>


<p align="right">
  <a href="#table-of-contents" > ↑ Back to top </a> 
</p>


## Getting Started

###  🟨 Installation
Depending on your OS, Concrete ML may be installed with Docker or with pip:

|               OS / HW                       | Available on Docker | Available on PyPI |
| :-----------------------------------------: | :-----------------: | :--------------: |
|                Linux                        |         Yes         |       Yes        |
|               Windows                       |         Yes         |    Coming soon   |
|     Windows Subsystem for Linux             |         Yes         |       Yes        |
|            macOS 11+ (Intel)                |         Yes         |       Yes        |
| macOS 11+ (Apple Silicon: M1, M2, etc.)     |         Yes         |       Yes        |


#### Docker
The preferred way to install Concrete is through PyPI:

```shell
pip install -U pip wheel setuptools
pip install concrete-python
```

#### Pip
You can get the concrete-python docker image by pulling the latest docker image:

```shell
docker pull zamafhe/concrete-python:v2.0.0
```

*Find more detailed installation instructions in [this part of the documentation](docs/getting-started/installing.md)*
<br></br>

### 🟨 A simple example
To compute on encrypted data, you first need to define the function you want to compute, then compile it into a Concrete Circuit, which you can use to perform homomorphic evaluation.
Here is the full example that we will walk through:

```python
from concrete import fhe

def add(x, y):
    return x + y

compiler = fhe.Compiler(add, {"x": "encrypted", "y": "encrypted"})
inputset = [(2, 3), (0, 0), (1, 6), (7, 7), (7, 1), (3, 2), (6, 1), (1, 7), (4, 5), (5, 4)]

print(f"Compiling...")
circuit = compiler.compile(inputset)

print(f"Generating keys...")
circuit.keygen()

examples = [(3, 4), (1, 2), (7, 7), (0, 0)]
for example in examples:
    encrypted_example = circuit.encrypt(*example)
    encrypted_result = circuit.run(encrypted_example)
    result = circuit.decrypt(encrypted_result)
    print(f"Evaluation of {' + '.join(map(str, example))} homomorphically = {result}")
```

Or if you have a simple function that you can decorate, and you don't care about explicit steps of key generation, encryption, evaluation and decryption:

```python
from concrete import fhe

@fhe.compiler({"x": "encrypted", "y": "encrypted"})
def add(x, y):
    return x + y

inputset = [(2, 3), (0, 0), (1, 6), (7, 7), (7, 1), (3, 2), (6, 1), (1, 7), (4, 5), (5, 4)]

print(f"Compiling...")
circuit = add.compile(inputset)

examples = [(3, 4), (1, 2), (7, 7), (0, 0)]
for example in examples:
    result = circuit.encrypt_run_decrypt(*example)
    print(f"Evaluation of {' + '.join(map(str, example))} homomorphically = {result}")
```
*This example is explained in more detail in the [Quick Start](docs/getting-started/quick_start).*

<p align="right">
  <a href="#table-of-contents" > ↑ Back to top </a> 
</p>

## Resources 

### 🟨 Tutorials

Various tutorials are proposed in the documentation to help you start writing homomorphic programs:

- How to use Concrete with [Decorators](https://docs.zama.ai/concrete/tutorials/decorator)
- Partial support of [Floating Points](https://docs.zama.ai/concrete/tutorials/floating_points)
- How to perform [Table Lookup](https://docs.zama.ai/concrete/tutorials/table_lookups)

If you have built awesome projects using Concrete, feel free to let us know and we'll link to it.
<br></br>


### 🟨 Documentation

Full, comprehensive documentation is available at [https://docs.zama.ai/concrete](https://docs.zama.ai/concrete).
<br></br>

<p align="right">
  <a href="#table-of-contents" > ↑ Back to top </a> 
</p>



## Working with Concrete

### 🟨 Contributing 

There are two ways to contribute to Concrete. You can:
- Open issues to report bugs and typos or suggest ideas;
- Request to become an official contributor by emailing hello@zama.ai. Only approved contributors can send pull requests (PRs), so get in touch before you do.
<br></br>

### 🟨 Citations
To cite Concrete in academic papers, please use the following entry:

```text
@Misc{Concrete,
  title={{Concrete: TFHE Compiler that converts python programs into FHE equivalent}},
  author={Zama},
  year={2022},
  note={\url{https://github.com/zama-ai/concrete}},
}
```

### 🟨 License
This software is distributed under the **BSD-3-Clause-Clear** license. If you have any questions, please contact us at hello@zama.ai.
<p align="right">
  <a href="#table-of-contents" > ↑ Back to top </a> 
</p>

## Support

<a target="_blank" href="https://community.zama.ai">
  <img src="https://github.com/zama-ai/concrete-ml/assets/157474013/9d518f66-30da-4738-a154-a4d9fce93704">
</a>
<br></br>


🌟 If you find this project helpful or interesting, please consider giving it a star on GitHub! Your support helps to grow the community and motivates further development. 🚀

[![GitHub stars](https://img.shields.io/github/stars/zama-ai/concrete.svg?style=social&label=Star)](https://github.com/zama-ai/concrete)

Thank you! 
<p align="right">
  <a href="#table-of-contents" > ↑ Back to top </a> 
</p>

