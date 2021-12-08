import os
import subprocess
import setuptools

from setuptools import Extension
from setuptools.command.build_ext import build_ext


def read(fname):
    return open(os.path.join(os.path.dirname(__file__), fname)).read()


class MakeExtension(Extension):
    def __init__(self, name, sourcedir=""):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


def build_dir():
    return os.environ.get("CONCRETE_COMPILER_BUILD_DIR", "build/")


class MakeBuild(build_ext):
    def run(self):
        for ext in self.extensions:
            self.build_extension(ext)

    def build_extension(self, ext):
        cmd = ["make", f"BUILD_DIR={build_dir()}"]
        py_exec = os.environ.get("CONCRETE_COMPILER_Python3_EXECUTABLE")
        if py_exec:
            cmd.append(f"Python3_EXECUTABLE={py_exec}")
        ccache = os.environ.get("CONCRETE_COMPILER_CCACHE")
        if ccache:
            cmd.append(f"CCACHE={ccache}")
        cmd.append("python-bindings")
        subprocess.check_call(cmd)


setuptools.setup(
    name="concretefhe-compiler",
    version="0.1.0",
    author="Zama Team",
    author_email="hello@zama.ai",
    description="Concrete Compiler",
    license="",
    keywords="homomorphic encryption compiler",
    long_description=read("README.md"),
    long_description_content_type="text/markdown",
    url="https://github.com/zama-ai/homomorphizer",
    packages=setuptools.find_packages(
        where=build_dir() + "tools/zamalang/python_packages/zamalang_core",
        include=["zamalang", "zamalang.*"],
    )
    + setuptools.find_namespace_packages(
        where=build_dir() + "tools/zamalang/python_packages/zamalang_core",
        include=["mlir", "mlir.*"],
    ),
    install_requires=["numpy"],
    package_dir={"": build_dir() + "tools/zamalang/python_packages/zamalang_core"},
    include_package_data=True,
    package_data={"": ["*.so"]},
    classifiers=[
        "Programming Language :: C++",
        "Programming Language :: Python :: 3",
        "Topic :: Software Development :: Compilers",
        "Topic :: Security :: Cryptography",
    ],
    ext_modules=[MakeExtension("python-bindings")],
    cmdclass=dict(build_ext=MakeBuild),
    zip_safe=False,
)
