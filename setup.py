from setuptools import Extension, setup


setup(
    ext_modules=[
        Extension(
            "bgpy.simulation_engine._announcement_c",
            sources=["bgpy/simulation_engine/_announcement_c.c"],
        )
    ]
)
