from setuptools import setup, find_packages
import os
from glob import glob

package_name = 'agribot_web'

setup(
    name=package_name,
    version='1.0.0',
    packages=find_packages(exclude=['frontend']),
    data_files=[
        ('share/ament_index/resource_index/packages', ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        (os.path.join('share', package_name, 'launch'), glob('launch/*.py')),
    ],
    install_requires=[
        'setuptools',
        'fastapi',
        'uvicorn',
    ],
    zip_safe=True,
    maintainer='agribot',
    maintainer_email='agribot@localhost',
    description='AgriBot web dashboard: sensor logging, REST API, and rosbridge launcher',
    license='MIT',
    entry_points={
        'console_scripts': [
            'sensor_logger = agribot_web.sensor_logger:main',
            'synthetic_moisture_node = agribot_web.synthetic_moisture_node:main',
            'web_server = agribot_web.web_server:main',
            'obstacle_avoidance = agribot_web.obstacle_avoidance:main',
        ],
    },
)
