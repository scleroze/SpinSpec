# SpinSpec example catalog

The web example picker is organized using [`examples/catalog.json`](catalog.json). The catalog preserves the physical `.set` filenames while providing clear display labels and section names.

## Categories

- **Basic spin systems** — minimal two- and three-spin exercises.
- **Fluorinated organic compounds** — molecules containing `19F` with proton–fluorine coupling models.
- **Organophosphorus compounds — full spin sets** — selected systems containing all proton and phosphorus spins represented in the molecular formula.
- **Organophosphorus compounds — simplified fragments** — reduced systems, not a complete molecular spin Hamiltonian.

## Scientific interpretation

**Important:** the ten newly added files dated 19 November 2026 are preliminary numerical examples. The complete original source conversation on J-coupling data was not available for direct inspection during catalog assembly. Accordingly, **their numerical J values have not yet been independently traced to literature or experimental sources** and must not be presented as validated reference constants.

Several files deliberately omit couplings, such as proton–proton coupling in fluorobenzene, or represent only one molecular branch. Those omissions are part of the illustrative model, not a claim that the physical coupling vanishes. Chemical shifts of zero may be **placeholders** rather than experimental values. Isotopes represented are `1H`, `19F` and `31P`, with a maximum of eight spins in the current web interface. Before reproducing a published spectrum, review all chemical shifts and J values against primary literature and restore any omitted couplings that materially affect the results.

In the fluorinated reference examples, the nucleus order has been standardized to list `1H` before `19F`. Corresponding coupling indices and display-spin selectors are permuted consistently. For example, 2,2,2-trifluoroethanol now uses `1H 1H 1H 19F 19F 19F`.

## Adding new examples

Place a correctly formed `.set` file in `examples/`. Add a record to the appropriate `groups[].items[]` collection in `catalog.json` with `filename`, human-readable `label` and `model` (`reference`, `illustrative`, or `reduced`). If an example has no catalog entry, the API retains it in an **Other examples** group.

Example values should be validated against reliable experimental or literature data before changing the model status to `reference`.
