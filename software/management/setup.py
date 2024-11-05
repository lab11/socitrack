import setuptools

with open('requirements.txt', encoding='utf8') as file:
   install_deps = [line for line in file]

with open('README.rst', 'r', encoding='utf8') as fh:
   long_description = fh.read()

setuptools.setup(
   name='tottag',
   version='1.0.4',
   author='Will Hedgecock',
   author_email='ronald.w.hedgecock@vanderbilt.edu',
   description='TotTag Management Dashboard',
   long_description=long_description,
   long_description_content_type='text/x-rst',
   url='https://github.com/lab11/socitrack',
   package_dir={'tottag': 'dashboard'},
   packages=['tottag'],
   include_package_data=True,
   install_requires=install_deps,
   classifiers=[
      'Programming Language :: Python :: 3',
      'Operating System :: OS Independent',
   ],
   python_requires='>=3.8',
   entry_points={
      'console_scripts': ['tottag = tottag.tottag:main'],
   }
)
