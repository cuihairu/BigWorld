"""CL-3 generator tests.

Three layers:

1. ``GoldenTest`` - regenerate every entity of the in-repo golden def tree
   (``programming/bigworld/examples/client_integration/python/simple/res/scripts``)
   and require byte-identical output to the checked-in reference under
   ``tests/golden/{cs,ts}``. This is the acceptance gate: the generator is
   deterministic and the wire-facing ids are frozen.
2. ``RuntimeConsistencyTest`` - the generated files shipped inside the CL-1
   (C#) and CL-2 (TS) runtime trees must equal what the generator produces
   today, so the runtime never silently drifts from the generator.
3. ``SyntheticTest`` - a small entity exercising the ARRAY / VECTOR / BLOB /
   unsupported-type branches that the golden tree does not hit, asserting the
   emitted structure (id slots reserved, TODO comments, no raw tabs).

Stdlib only (unittest); run from the repo root:

    python3 -m unittest discover -s client_sdk/generator/tests -v
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
GOLDEN = Path(__file__).resolve().parent / "golden"
SCRIPTS = REPO / "programming/bigworld/examples/client_integration/python/simple/res/scripts"

sys.path.insert(0, str(REPO / "client_sdk" / "generator"))

from bwgen.defparse import load_tree            # noqa: E402
from bwgen.emit_cs import emit_entity as emit_cs  # noqa: E402
from bwgen.emit_ts import emit_entity as emit_ts  # noqa: E402
from bwgen.ids import client_method_ids, client_properties, exposed_method_ids  # noqa: E402
from bwgen.model import EntityDef, MethodDef, PropertyDef, TypeRef  # noqa: E402
from bwgen.typemap import resolve              # noqa: E402


class GoldenTest(unittest.TestCase):
    """Byte-identical regeneration of the golden def tree."""

    def test_regeneration_matches_reference(self):
        tree = load_tree(str(SCRIPTS))
        self.assertTrue(tree.entities, "golden tree must parse to entities")
        seen = set()
        for entity in tree.entities:
            cs = emit_cs(entity)
            ts = emit_ts(entity)
            seen.add(entity.name)
            self._assert_no_raw_tabs(cs, "%sBase.cs" % entity.name)
            self._assert_no_raw_tabs(ts, "%sBase.ts" % entity.name)
            self.assertEqual(
                cs, self._golden("cs", "%sBase.cs" % entity.name))
            self.assertEqual(
                ts, self._golden("ts", "%sBase.ts" % entity.name))
        # the golden tree must contain every generated entity
        for path in sorted(GOLDEN.glob("cs/*.cs")):
            self.assertIn(path.stem[:-4], seen, "orphan golden %s" % path)

    @staticmethod
    def _golden(sub, name):
        return (GOLDEN / sub / name).read_text()

    @staticmethod
    def _assert_no_raw_tabs(text, what):
        if "\t" in text:
            raise AssertionError("%s contains a raw tab" % what)


class RuntimeConsistencyTest(unittest.TestCase):
    """The generated files shipped in the runtimes equal fresh output."""

    def test_unity_generated_matches(self):
        tree = {e.name: e for e in load_tree(str(SCRIPTS)).entities}
        for name in ("Account", "ClientAvatar"):
            path = REPO / "client_sdk/unity/src/BigWorld.ClientSdk/Generated" / ("%sBase.cs" % name)
            self.assertEqual(emit_cs(tree[name]), path.read_text())

    def test_typescript_generated_matches(self):
        tree = {e.name: e for e in load_tree(str(SCRIPTS)).entities}
        for name in ("Account", "ClientAvatar"):
            path = REPO / "client_sdk/typescript/src/generated" / ("%sBase.ts" % name)
            self.assertEqual(emit_ts(tree[name]), path.read_text())


class WireIdTest(unittest.TestCase):
    """The frozen wire-facing id numbering for the golden entities."""

    def test_account_ids(self):
        tree = {e.name: e for e in load_tree(str(SCRIPTS)).entities}
        acct = tree["Account"]
        self.assertEqual(
            [p.name for p in client_properties(acct)], ["playerName"])
        self.assertEqual(client_method_ids(acct), {"chatMessage": 0})

    def test_client_avatar_ids(self):
        tree = {e.name: e for e in load_tree(str(SCRIPTS)).entities}
        av = tree["ClientAvatar"]
        # size-sorted client-server properties: fixed asc (prop3, prop2,
        # prop1) then variable asc (playerName, prop5 - both -1, stable).
        self.assertEqual(
            [p.name for p in client_properties(av)],
            ["prop3", "prop2", "prop1", "playerName", "prop5"])
        # Client method ids come from the JOINT exposed id space: the server
        # stable-sorts all exposed methods (base, cell, client) together -
        # logOff (fixed 0B)=0, cellTick (fixed 4B)=1, fromCell (variable)=2.
        self.assertEqual(client_method_ids(av),
                         {"fromCell": 2, "cellTick": 1})
        # The base exposed method shares that same joint id space.
        self.assertEqual(exposed_method_ids(av.base_methods, av),
                         {"logOff": 0})


class SyntheticTest(unittest.TestCase):
    """Codegen branches the golden tree does not exercise."""

    def _entity(self):
        props = [
            PropertyDef("numbers", TypeRef("ARRAY", TypeRef("INT32")),
                         flags="BASE_AND_CLIENT"),
            PropertyDef("pos", TypeRef("VECTOR3"), flags="BASE_AND_CLIENT"),
            PropertyDef("payload", TypeRef("BLOB"), flags="BASE_AND_CLIENT"),
            PropertyDef("label", TypeRef("STRING"), flags="BASE_AND_CLIENT"),
            PropertyDef("opaque", TypeRef("USER_TYPE"), flags="ALL_CLIENTS"),
        ]
        methods = [
            MethodDef("push", "client", [
                PropertyDef("vals", TypeRef("ARRAY", TypeRef("INT16"))),
                PropertyDef("point", TypeRef("VECTOR2")),
            ]),
            MethodDef("poke", "client", [
                PropertyDef("raw", TypeRef("BLOB")),
            ]),
            MethodDef("call", "base", [
                PropertyDef("tag", TypeRef("STRING")),
                PropertyDef("raw", TypeRef("BLOB")),
                PropertyDef("vals", TypeRef("ARRAY", TypeRef("INT32"))),
            ], exposed=True),
        ]
        return EntityDef(name="Synth", properties=props,
                         client_methods=methods[:2],
                         base_methods=methods[2:])

    def test_cs_branches(self):
        text = emit_cs(self._entity())
        self.assertIn("PropertyIdLabel = 3", text)   # label is client-visible
        self.assertIn("PropertyIdOpaque = 4", text)  # id slot still reserved
        self.assertIn("TODO(batch2)", text)
        self.assertNotIn("\t", text)
        # ARRAY read: packed length + loop
        self.assertIn("int count = reader.ReadPackedInt();", text)
        # VECTOR3 read: three floats
        self.assertIn("reader.ReadFloat()", text)
        # Send stub writes: string, blob (packed length + raw), array (packed
        # length + element loop) - args are positional value0..value2.
        self.assertIn("writer.WriteString(value0);", text)
        self.assertIn("writer.WritePackedInt(value1.Length);", text)
        self.assertIn("writer.WriteBlob(value1);", text)
        self.assertIn("writer.WritePackedInt(value2.Length);", text)
        self.assertIn("for (int i = 0; i < value2.Length; ++i)", text)
        # BLOB property read: packed length then raw bytes
        self.assertIn("Payload = reader.ReadBytesRaw(reader.ReadPackedInt());", text)

    def test_ts_branches(self):
        text = emit_ts(self._entity())
        self.assertIn("propertyIdLabel = 3", text)
        self.assertIn("propertyIdOpaque = 4", text)
        self.assertIn("TODO(batch2)", text)
        self.assertNotIn("\t", text)
        self.assertIn("reader.readPackedInt()", text)
        self.assertIn("reader.readFloat()", text)
        self.assertIn("call.writer.writePackedInt(value1.length);", text)
        self.assertIn("call.writer.writeBlob(value1);", text)
        self.assertIn("call.writer.writePackedInt(value2.length);", text)
        self.assertIn("this.payload = reader.readBytesRaw(reader.readPackedInt());", text)

    def test_unsupported_marks(self):
        resolved = resolve(TypeRef("USER_TYPE"))
        self.assertFalse(resolved.supported)
        self.assertEqual(resolve(TypeRef("STRING")).supported, True)
        self.assertEqual(resolve(TypeRef("INT32")).wire_size, 4)
        self.assertEqual(resolve(TypeRef("STRING")).stream_size, -1)

    def test_joint_exposed_ids_across_families(self):
        # base + cell + client exposed methods share ONE id space, sorted
        # fixed-ascending then variable-ascending (stable, base -> cell ->
        # client pre-sort order).
        entity = EntityDef(
            name="Joint",
            base_methods=[
                MethodDef("doLog", "base", exposed=True),           # fixed 0B
                MethodDef("doBig", "base", [PropertyDef("n", TypeRef("INT64"))],
                          exposed=True),                            # fixed 8B
            ],
            cell_methods=[
                MethodDef("doMid", "cell",
                          [PropertyDef("n", TypeRef("INT16"))],
                          exposed=True),                            # fixed 2B
            ],
            client_methods=[
                MethodDef("onVar", "client",
                          [PropertyDef("s", TypeRef("STRING"))]),   # variable
                MethodDef("onTick", "client",
                          [PropertyDef("n", TypeRef("INT32"))]),    # fixed 4B
            ],
        )
        ids = client_method_ids(entity)
        # joint sort: doLog(0), doMid(2), onTick(4), doBig(8), onVar(variable)
        self.assertEqual(ids, {"onTick": 2, "onVar": 4})
        self.assertEqual(exposed_method_ids(entity.base_methods, entity),
                         {"doLog": 0, "doBig": 3})
        self.assertEqual(exposed_method_ids(entity.cell_methods, entity),
                         {"doMid": 1})


if __name__ == "__main__":
    unittest.main()
