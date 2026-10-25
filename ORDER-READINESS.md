# Prototype order sign-off

Run `./check-order.sh` before uploading or paying for an order. It checks the
existing release without rebuilding the PCB or rewriting manufacturing files.
Exit code 0 means automated checks and recorded prototype reviews pass; 1 means
an automated check failed; 2 means automated checks pass but external reviews
remain. `./check-order.sh --automated-only` ignores pending reviews for its exit
code, while still printing them. Neither mode certifies a working product.

## What the command checks

- Release hashes cover the board, schematic, design rules, generator sources,
  project footprints, BOM/CPL, Gerbers, schematic PDF, assembly and mechanical maps.
- Fresh ERC and zone-refilled DRC, including schematic parity. Only the documented
  U1/J1 footprint mismatch warnings are permitted. Additional ignored DRC
  categories fail the check. Existing ignored categories remain limitations.
- Circuit, protected routing, display spacing and ground-reference checks.
- Each assembled reference has the exact LCSC ID, value and footprint from the
  design on the PCB and in the BOM. No missing or duplicate references.
- Every CPL position, side and angle matches the actual PCB. This checks KiCad's
  export convention; it does not establish the assembler's rotation convention.
- All expected copper, mask, paste, silkscreen, outline and drill files are
  present in the fabrication archive, nonempty and readable.

The manifest detects changes since export. It does not prove that the circuit,
manufacturer's part selection or impedance target is correct. Use `./build.sh`
after hardware edits; manually rehashing stale files is not a rebuild.

## External checks still needed

Record these in `order-review.json`. Every completed check needs `status: "pass"`,
the reviewer's name, date, and specific evidence (order/quote ID, saved preview,
part number, photograph or measurement reference). Leave unperformed checks
`pending`. Set `release_sha256` to the digest printed by `./check-order.sh` only
when reviewing that exact release. A changed digest invalidates the old review.

| Check | Evidence to collect |
|---|---|
| Fabricator | Quote confirms 42 × 98 mm, four layers, nominal 1.6 mm JLC04161H-7628 construction, 1 oz outer / 0.5 oz inner copper, 90-ohm USB target, epoxy-filled and copper-capped vias, and unplated mounting holes. Review layer/drill preview. |
| Assembly | Exact BOM parts available; any substitution reviewed. Check every orientation against `fab/assembly-pin1.csv` and the front/back maps. Choose both-side assembly or explicitly plan hand fitting SW1–SW5, D5, R4 and C40. Confirm J3/J5/DS1 excluded and R23–R25 fitted. |
| Display fit | Actual GDEY0213B74 tail reach, pin-1 direction and connector insertion checked on a 1:1 mock-up. If only the prototype can resolve this, record that fit is an accepted prototype experiment; it is still required before a batch. |
| Battery fit | Exact protected 1S 4.2 V pack, manufacturer permits at least 250 mA charge and 1 A pulse discharge. Record actual lead polarity and an insulated placement clear of components and antenna. |
| Prototype plan | Record small first-run quantity, access to a current-limited supply and measurement equipment, and a firmware bring-up plan. No application firmware exists in this repository. |

`BRINGUP.md` is the next gate after delivery. Record measurements against the board
revision and firmware version before approving a larger batch. Supplier previews,
physical fit, temperature, radio performance and actual operation cannot be
signed off by a passing script.
