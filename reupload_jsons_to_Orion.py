"""
This file is meant to upload the Orion objects stored in JSON to the
Orion broker before testing with a physical or virtual PLC.

All files of the "json" directory are uploaded.
"""

import json
import glob

# PyPI imports

# Custom imports
import Orion

def main():
    jsons = glob.glob('json/*.json')
    objects = []
    for json_ in jsons:
        with open(json_, 'r') as f:
            obj = json.load(f)
        objects.append(obj)
    Orion.update(objects)

if __name__ == '__main__':
    main()

