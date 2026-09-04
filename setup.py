from setuptools import setup

setup(
    name="cbpi4_hopdropper",
    version="0.0.1",
    description="CraftBeerPi 4 Plugin für einen MQTT Hopdropper mit 5 Slots",
    author="Holm Sommer",
    author_email="",
    url="",
    license="GPLv3",
    include_package_data=True,
    package_data={"cbpi4_hopdropper": ["config.yaml"]},
    packages=["cbpi4_hopdropper"],
    install_requires=[],
    long_description="""CraftBeerPi 4 Plugin für einen MQTT gesteuerten Hopdropper mit 5 Slots.""",
)
