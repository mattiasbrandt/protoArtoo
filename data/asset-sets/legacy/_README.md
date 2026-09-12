Legacy asset set (ADR 0065, #316).

This directory exists so `artoo_esp32` (custom_asset_set = legacy) does not
fail the staging step now that `data/asset-sets/` is present. It carries
**line drawings** in place of the default set's photographs; those drawings
are #382's and are not this ticket's.

A file whose name starts with `_` is a partial and is not imaged
(tools/gzip_fsdata.py), so this note costs the 4 MB board nothing.
