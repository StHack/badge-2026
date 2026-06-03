"""
/Applications/FreeCAD.app/Contents/Resources/bin/freecadcmd \
  step2stl.py \
  --pass badge.step badge.stl
"""

import sys
import FreeCAD
import Part
import Mesh

args = sys.argv
if "--pass" in args:
    args = args[args.index("--pass") + 1:]
else:
    args = args[1:]

input_file = args[0]
output_file = args[1]

shape = Part.Shape()
shape.read(input_file)

doc = FreeCAD.newDocument()
part = doc.addObject("Part::Feature", "Part")
part.Shape = shape

Mesh.export([part], output_file)

print(f"Converti : {input_file} -> {output_file}")
