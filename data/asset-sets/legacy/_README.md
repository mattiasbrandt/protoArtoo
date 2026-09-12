Legacy asset set (ADR 0065, #382).

The narrower of the two sets: it carries **line drawings** where the default set
carries photographs. The name describes what the set holds and never the board
that carries it -- the artoo-esp32 is not a legacy board.

The drawings are one inlined sprite, `_product_art.html`, rather than a file
each, because littlefs charges a 4 KiB block per file whatever the content. That
file's own header says the rest.

A file whose name starts with `_` is a partial and is not imaged
(tools/gzip_fsdata.py), so neither this note nor the sprite costs the 4 MB board
anything until a page inlines it.
