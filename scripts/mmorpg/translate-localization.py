"""Fill the UE-exported PO from reviewed translations; never synthesize a gather manifest."""
import argparse
import ast
import json
import re
from pathlib import Path


def po_value(block, field):
    match = re.search(rf'^{re.escape(field)} ("(?:\\.|[^"\\])*")((?:\n"(?:\\.|[^"\\])*")*)', block, re.M)
    if not match:
        return None
    return "".join(ast.literal_eval(s) for s in [match.group(1), *match.group(2).splitlines()] if s)


def po_identity(context):
    """Match UE PortableObjectPipeline::ParseIdentityFromPO's unescaped comma."""
    if context:
        escaped = False
        for index, character in enumerate(context):
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == ",":
                namespace = context[:index].replace("\\,", ",")
                key = context[index + 1:].replace("\\,", ",")
                return re.sub(r"\s*\[[^\]]+\]$", "", namespace), key
    raise ValueError(f"Expected Unreal PO namespace,key context: {context!r}")


def translate(project, examples):
    ui = json.loads((examples / "ui-designer-strings.json").read_text(encoding="utf-8-sig"))
    entries = {}
    for entry in ui["entries"]:
        identity = (ui["namespace"], entry["key"])
        if identity in entries:
            raise ValueError(f"Duplicate Designer StringTable key: {identity}")
        entries[identity] = (entry["source"], entry["zh-Hans"])
    runtime = json.loads((examples / "localization-runtime-zh-Hans.json").read_text(encoding="utf-8-sig"))
    found_keys = set()
    # Keep this review scope aligned with MMO_Gather.ini. Unreal's gather output
    # is still authoritative; unsupported/new macro forms fail as unreviewed PO
    # entries rather than silently receiving guessed translations.
    source_roots = [project / "Source/MMORPG", project / "Plugins/MMOFramework/Source"]
    sources = sorted({source for root in source_roots for source in root.rglob("*")
                      if source.is_file() and source.suffix in (".h", ".cpp")})
    for cpp in sources:
        code = cpp.read_text(encoding="utf-8-sig")
        namespace = re.search(r'#define\s+LOCTEXT_NAMESPACE\s+"([^"]+)"', code)
        literals = []
        if namespace:
            literals += [(namespace[1], key, literal) for key, literal in re.findall(
                r'\bLOCTEXT\(\s*"([^"]+)"\s*,\s*("(?:\\.|[^"\\])*")\s*\)', code)]
        literals += re.findall(
            r'\bNSLOCTEXT\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*("(?:\\.|[^"\\])*")\s*\)', code)
        for text_namespace, key, literal in literals:
            source = ast.literal_eval(literal)
            if text_namespace != runtime["namespace"] or key not in runtime["translations"]:
                raise ValueError(f"Missing reviewed runtime translation: {text_namespace}/{key}")
            reviewed = runtime["translations"][key]
            if not isinstance(reviewed, dict) or "source" not in reviewed or "zh-Hans" not in reviewed:
                raise ValueError(f"Runtime translation must include reviewed source and zh-Hans: {text_namespace}/{key}")
            if source != reviewed["source"]:
                raise ValueError(f"C++ source changed since translation review: {text_namespace}/{key}")
            identity = (text_namespace, key)
            value = (reviewed["source"], reviewed["zh-Hans"])
            if identity in entries and entries[identity] != value:
                raise ValueError(f"Conflicting localization identity/source: {text_namespace}/{key}")
            entries[identity] = value
            found_keys.add(key)
    po = project / "Content/Localization/MMO/zh-Hans/MMO.po"
    blocks = re.split(r"\n\s*\n", po.read_text(encoding="utf-8-sig"))
    translated, covered = [], set()
    for block in blocks:
        source = po_value(block, "msgid")
        if not source:  # PO header and trailing empty block
            translated.append(block)
            continue
        context = po_value(block, "msgctxt")
        namespace, key = po_identity(context)
        entry = entries.get((namespace, key))
        if not entry or entry[0] != source:
            raise ValueError(f"Unreviewed or changed gathered text: {context}: {source!r}")
        target = entry[1]
        if not isinstance(target, str) or not target.strip():
            raise ValueError(f"Empty reviewed translation: {context}")
        if sorted(re.findall(r"\{[^{}]+\}", source)) != sorted(re.findall(r"\{[^{}]+\}", target)):
            raise ValueError(f"Format arguments differ: {context}")
        replacement = "msgstr " + json.dumps(target, ensure_ascii=False)
        block, count = re.subn(r'^msgstr "(?:\\.|[^"\\])*"(?:\n"(?:\\.|[^"\\])*")*', lambda _: replacement, block, flags=re.M)
        if count != 1:
            raise ValueError(f"Expected one non-plural msgstr: {context}")
        translated.append(block)
        covered.add((namespace, key))
    missing = set(entries) - covered
    if missing:
        raise ValueError(f"Gather did not include {len(missing)} expected entries: {sorted(missing)}")
    po.write_text("\n\n".join(translated).rstrip() + "\n", encoding="utf-8")
    return {"passed": True, "nativeCulture": "en", "culture": "zh-Hans",
            "designerKeys": len(ui["entries"]), "runtimeKeys": len(found_keys),
            "translatedKeys": len(covered), "reviewedSourceFiles": len(sources), "po": str(po)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--examples", type=Path, default=Path(__file__).resolve().parents[2] / "examples/MMORPG")
    args = parser.parse_args()
    print(json.dumps(translate(args.project, args.examples), ensure_ascii=False, indent=2))
