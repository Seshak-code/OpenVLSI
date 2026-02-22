import sys
import re

def patch():
    file_path = "build-wasm/AcuteSim.js"
    with open(file_path, "r", encoding='utf-8') as f:
        text = f.read()

    # Float converter
    text = re.sub(
        r'(\s*"toWireType":\s*\(destructors,\s*value\)\s*=>\s*\{\s*)(if\s*\(\s*typeof\s*value\s*!=\s*"number"\s*&&\s*typeof\s*value\s*!=\s*"boolean"\s*\)\s*\{)',
        r'\1if (value == null) return 96.0;\n\2',
        text
    )

    # Integer converter
    text = re.sub(
        r'(\s*"toWireType":\s*function\s*\(destructors,\s*value\)\s*\{\s*)(if\s*\(\s*typeof\s*value\s*!=\s*"bigint"\s*&&\s*typeof\s*value\s*!=\s*"number"\s*\)\s*\{)',
        r'\1if (value == null) return 0;\n\2',
        text
    )

    # Unsigned Integer converter (checkAssertions)
    text = re.sub(
        r'(\s*var\s*checkAssertions\s*=\s*\(value,\s*toTypeName\)\s*=>\s*\{\s*)(if\s*\(\s*typeof\s*value\s*!=\s*"number"\s*&&\s*typeof\s*value\s*!=\s*"boolean"\s*\)\s*\{)',
        r'\1if (value == null) return;\n\2',
        text
    )

    # String converter 1
    text = re.sub(
        r'(\s*"toWireType"\s*\(destructors,\s*value\)\s*\{\s*)(if\s*\(\s*value\s*instanceof\s*ArrayBuffer\s*\)\s*\{)',
        r'\1if (value == null) value = "";\n\2',
        text
    )

    # String converter 2
    text = re.sub(
        r'(\s*"toWireType":\s*\(destructors,\s*value\)\s*=>\s*\{\s*)(if\s*\(\!\(typeof\s*value\s*==\s*"string"\)\)\s*\{)',
        r'\1if (value == null) value = "";\n\2',
        text
    )

    # Emval get property
    text = re.sub(
        r'(\s*function\s*__emval_get_property\s*\(handle,\s*key\)\s*\{[\s\S]*?key\s*=\s*Emval\.toValue\s*\(key\);\s*)(return\s*Emval\.toHandle\s*\(handle\[key\]\);\s*\})',
        r'\1if (handle == null) return Emval.toHandle(undefined);\n\2',
        text
    )

    # EmscriptenEH class
    if "class EmscriptenEH" not in text:
        text = text.replace("var AcuteSim_entry = (() => {", "class EmscriptenEH { constructor(ptr) { this.ptr = ptr; } }\nvar AcuteSim_entry = (() => {")

    with open(file_path, "w", encoding='utf-8') as f:
        f.write(text)

    print("Regex Patch applied successfully.")

if __name__ == "__main__":
    patch()
