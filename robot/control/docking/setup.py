from setuptools import find_packages, setup


package_name = "shalom_docking"

setup(
    name=package_name,
    version="0.1.0",
    packages=find_packages(exclude=["test"]),
    data_files=[
        ("share/ament_index/resource_index/packages", ["resource/" + package_name]),
        ("share/" + package_name, ["package.xml", "README.md"]),
    ],
    install_requires=["setuptools"],
    tests_require=["pytest"],
    zip_safe=True,
    maintainer="Wego Robotics",
    maintainer_email="junoyoo@wego-robotics.com",
    description="Generate and reload Nav2 dock databases from map-owned locations.",
    license="Proprietary",
    entry_points={
        "console_scripts": [
            "reload_dock_database = shalom_docking.reload:main",
        ],
    },
)
