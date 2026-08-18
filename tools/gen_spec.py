#!/usr/bin/env python3
"""OES — external configuration-spec generator (demo).

Emits a friendly JSON configuration spec on stdout. This proves the "external
script, no compiler" path: pipe the output to a .json file and feed it to
oes_config_gen, which builds the .mcf.

    python tools/gen_spec.py > samples/sample_config.json
    build/.../bin/Release/oes_config_gen samples/sample_config.json samples/SampleConfig.mcf
"""
import json
import sys

VES_MODULE = (
    "// Sample common module - VES (1C/BSL-flavoured) dialect.\n\n"
    "Function CalculateLineTotal(Quantity, Price) Export\n"
    "\tReturn Quantity * Price;\n"
    "EndFunction\n\n"
    "Function ApplyDiscount(Amount, Percent) Export\n"
    "\tIf Percent <= 0 Then\n\t\tReturn Amount;\n\tEndIf;\n"
    "\tReturn Amount - Amount * Percent / 100;\n"
    "EndFunction\n\n"
    "Procedure ShowGreeting(Name) Export\n"
    "\tIf Name = \"\" Then\n\t\tMessage(\"Hello, guest!\");\n"
    "\tElse\n\t\tMessage(\"Hello, \" + Name + \"!\");\n\tEndIf;\n"
    "EndProcedure\n"
)


def build_spec():
    return {
        "name": "SampleConfiguration",
        "catalogs": [
            {
                "name": "Products",
                "attributes": [
                    {"name": "Price", "type": "Number", "precision": 15, "scale": 2},
                    {"name": "Article", "type": "String", "length": 20},
                ],
            }
        ],
        "documents": [
            {
                "name": "Invoice",
                "attributes": [
                    {"name": "Sum", "type": "Number", "precision": 15, "scale": 2},
                    {"name": "Date", "type": "Date"},
                ],
            }
        ],
        "commonModules": [
            {"name": "CommonFunctions", "code": VES_MODULE},
        ],
    }


if __name__ == "__main__":
    json.dump(build_spec(), sys.stdout, ensure_ascii=False, indent=2)
    sys.stdout.write("\n")
