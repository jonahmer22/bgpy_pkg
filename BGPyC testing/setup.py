from setuptools import setup, Extension

setup(
    name="bgpyc",
    version="0.0.1",
    ext_modules=[
        Extension("bgpyc", sources=["bgpyc.c"]),
    ],
)