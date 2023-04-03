all:
	#python3 setup.py sdist bdist_wheel
	shiv -c tottag -o tottag .
	#shiv --site-packages dist --compressed -c tottag -o tottag -e tottag.__main__:main
	#shiv -c tottag -o tottag -r requirements.txt --site-packages dist -e tottag.tottag:main

clean:
	rm -rf build dist tottag *.egg-info
