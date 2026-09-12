# Product image provenance

Photographs the **default** asset set carries for the Component Picker
(#316, ADR 0065). Each file is `data/asset-sets/default/<id>.webp`,
where `<id>` is the Component Registry token. The **legacy** set does not
carry these files.

They are identification pictures: the thing an operator matches against
the hardware in their hand. They are not a grant of rights in the product
or its design. Third-party photographs stay with their owners; see
LICENSE scope item 4.

Encoded 400x300 WebP, dark ground `#0c1525`, at most 8 KiB
(`tools/encode_product_photos.py`). Originals live in the local
`tasks/product-images/` directory and are not committed.

| File | Product | Status | Source | Licence |
|---|---|---|---|---|
| `artoo_pcb.webp` | Artoo PCB (artoo.uk) | supported | Operator-supplied product photograph of Steve Wagg's Artoo Controller v1.1 with an ESP32 D1 Mini seated. The board design is Steve's (LICENSE item 2, https://artoo.uk). | Used with the hardware this firmware already credits. |
| `firebeetle2.webp` | FireBeetle 2 (ESP32-P4) | supported | Operator-supplied DFRobot product photograph of the FireBeetle 2 ESP32-P4 and its carrier. | Manufacturer product image, used to identify the board. |
| `hotrc_ds650.webp` | HotRC DS-650 | supported | Operator-supplied listing photograph of a pistol-grip transmitter (branded Havcybin on this shot) with a HotRC F-06A in frame. The registry row is the HotRC DS-650 handset. | Manufacturer / listing photograph, used to identify the class of handset. |
| `rc_transmitter_pwm.webp` | RC Transmitter - PWM | supported | Operator-supplied product photograph of a HotRC F-06A PWM receiver. The row is a receiver, not a transmitter — recorded here because the photograph is of the box in the droid, which is what protoArtoo talks to. | Manufacturer product image, used to identify the receiver. |
| `rc_transmitter_sbus.webp` | RC Transmitter - SBUS | supported | Operator-supplied product photograph of an SBUS receiver. Same receiver-not-transmitter note as the PWM row. | Manufacturer product image, used to identify the receiver. |
| `rc_transmitter_elrs.webp` | RC Transmitter - ELRS | roadmap | Operator-supplied product photograph of an ELRS receiver (SuperP-class). Same receiver-not-transmitter note. `tasks/product-images/elrs-radio.png` is a handset and was not encoded. | Manufacturer product image, used to identify the receiver. |
| `xbox_controller.webp` | Xbox Controller | roadmap | Operator-supplied photograph of a white Xbox 360 wired controller. EXIF: Nikon D7000, Adobe Photoshop CS5, 2014-08-29. Photographer not named on the file. | Third-party photograph; copyright holder unknown. Used only as product identification on a picker card. |
| `pca9685.webp` | PCA9685 | roadmap | Operator-supplied product photograph of a 16-channel PCA9685 expander board. | Manufacturer / listing photograph, used to identify the board. |
| `pololu_maestro.webp` | Pololu Maestro | roadmap | Operator-supplied product photograph of a Pololu Maestro. | Manufacturer product image, used to identify the board. |
| `isdt_esc70.webp` | ISDT ESC70 (RC ESC) | supported | Operator-supplied product photograph of an ISDT ESC70. | Manufacturer product image, used to identify the ESC. |
| `syren10.webp` | SyRen 10 | roadmap | Operator-supplied product photograph of a Dimension Engineering SyRen 10. | Manufacturer product image, used to identify the controller. |
| `astropixels_plus.webp` | AstroPixels Plus | supported | Operator-supplied photograph of an AstroPixels Plus board. | Used to identify the dome controller this project already speaks to. |
| `teeces.webp` | Teeces | roadmap | Operator-supplied photograph of a Teeces rear logic display. EXIF: Adobe Photoshop Lightroom Classic 13.5.1, 2025-01-14. | Third-party photograph of the board, used to identify it. |
| `hoverboard.webp` | Hoverboard, hacked firmware | supported | Operator-supplied catalogue photograph of a complete Swagtron hoverboard, not of the salvaged dual-motor board protoArtoo actually talks to. Kept because it is the product a builder recognises on a bench. | Manufacturer product image, used to identify the donor vehicle. |
| `sabertooth_2x25.webp` | Sabertooth 2x25 | roadmap | Operator-supplied product photograph of a Dimension Engineering Sabertooth 2x25. | Manufacturer product image, used to identify the controller. |
| `flipsky_mini_v6_vesc.webp` | Flipsky Mini V6 VESC | roadmap | Operator-supplied product photograph of a Flipsky Mini V6. | Manufacturer product image, used to identify the controller. |
| `dy_sv5w.webp` | DY-SV5W | supported | Operator-supplied product photograph of a DY-SV5W. | Manufacturer product image, used to identify the module. |
| `mp3_trigger.webp` | MP3 Trigger | supported | Operator-supplied product photograph of a SparkFun MP3 Trigger. | Manufacturer product image, used to identify the module. |
| `chirp.webp` | CHIRP Audio Trigger | supported | Operator-supplied 3D render of the CHIRP Audio Trigger Rev B (silkscreen 5C17V, dated 20260101). | Project render of the module this firmware already drives. |
| `dfplayer_mini.webp` | DFPlayer Mini | roadmap | Operator-supplied product photograph of a DFPlayer Mini. EXIF: Adobe Photoshop 22.0, 2024-12-31. | Manufacturer product image, used to identify the module. |

Not photographed:

| Product | Why |
|---|---|
| ESP32 GPIO (LEDC) | The MCU's own PWM. There is no separate product to hold. |

The PWM / SBUS / ELRS registry labels still read **RC Transmitter**. The
photographs are of the receivers those rows actually name. Relabelling the
operator-facing strings is copy, which #316 leaves to #297 / #298; the
operator recorded the misnomer on this ticket on 2026-09-12.
