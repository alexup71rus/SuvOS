#!/usr/bin/python3

import platform
import os


def main() -> None:
    if os.environ.get("SUVOS_LANG", "ru").split("_", 1)[0] == "en":
        print("Hello from Python inside SuvOS.")
    else:
        print("Привет из Python внутри SuvOS.")
    print(f"Python: {platform.python_version()}")


if __name__ == "__main__":
    main()
