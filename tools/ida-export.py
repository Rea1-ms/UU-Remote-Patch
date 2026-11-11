"""Export selected decompilations from a user-owned local binary."""
import json
import os
import ida_auto
import ida_funcs
import ida_hexrays
import ida_nalt
import ida_pro
import idautils
import idc

fast = "--fast" in idc.ARGV
if fast:
    ida_auto.enable_auto(False)
else:
    ida_auto.auto_wait()
base = ida_nalt.get_imagebase()
output = idc.ARGV[1]
targets = [arg for arg in idc.ARGV[2:] if arg != "--fast"]
records = []
available = ida_hexrays.init_hexrays_plugin()
for target in targets:
    addresses = []
    if target.startswith("range:"):
        low, high = (base + int(value, 16) for value in target.split(":")[1:])
        addresses.extend(idautils.Functions(low, high))
    elif target.startswith("xrefs:"):
        address = base + int(target.split(":")[1], 16)
        for ref in idautils.XrefsTo(address):
            records.append({"target": target, "reference": hex(ref.frm-base), "type": ref.type})
            addresses.append(ref.frm)
    elif target.startswith("name:"):
        needle = target[5:]
        addresses.extend(ea for ea, name in idautils.Names() if needle in name)
    elif target.startswith("0x"):
        addresses.append(base + int(target, 16))
    else:
        for item in idautils.Strings():
            if target in str(item):
                addresses.extend(ref.frm for ref in idautils.XrefsTo(item.ea))
    for address in dict.fromkeys(addresses):
        function = ida_funcs.get_func(address)
        if not function:
            records.append({"target": target, "rva": hex(address-base), "error": "no function"})
            continue
        try:
            code = str(ida_hexrays.decompile(function.start_ea)) if available else "decompiler unavailable"
        except Exception as error:
            code = str(error)
        records.append({"target": target, "rva": hex(function.start_ea-base), "code": code})
with open(output, "w", encoding="utf-8") as stream:
    json.dump(records, stream, ensure_ascii=False, indent=2)
ida_pro.qexit(0)
