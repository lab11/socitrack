ifeq ($(OS),Windows_NT)
	BREW_CMD :=
	PYTHON_CMD := python -m pip install .
	UNINSTALL_CMD := python -m pip uninstall -y tottag
else
	UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Linux)
		BREW_CMD :=
		PYTHON_CMD := python3 -m pip install .
		UNINSTALL_CMD := python3 -m pip uninstall -y tottag
	else
		BREW_CMD := brew install python-tk
		PYTHON_CMD := python3 -m pip install --break-system-packages .
		UNINSTALL_CMD := python3 -m pip uninstall -y --break-system-packages tottag
	endif
endif


all:
	$(BREW_CMD)
	$(PYTHON_CMD)
	rm -rf build dist tottag* *.egg-info __pycache__ dashboard/__pycache__

clean:
	rm -rf build dist tottag* *.egg-info __pycache__ dashboard/__pycache__

publish:
	python3 -m build
	python3 -m twine upload dist/*

uninstall:
	$(UNINSTALL_CMD)
