# notebooks/ — reproducible experiments

They produce the plots from real measurements (compile small C harnesses against
the header, or run the model). Needs `gcc`, `matplotlib` and `numpy`.

| Notebook | Measures |
|---|---|
| `01_cache_cliff.ipynb`    | cache cliff: `max` speedup (u8 vs int64) vs N |
| `02_pfor.ipynb`           | PFOR memory recovery vs outlier fraction |
| `03_zonemap.ipynb`        | free zone-map: % columns/bandwidth skipped (model) |
| `04_ai_blocked.ipynb`     | AI: per-channel vs per-token memory vs outlier fraction |
| `05_capacity_model.ipynb` | server capacity model (estimate) |

The harnesses locate the header by walking up from `notebooks/` until they find
`include/`. Regenerate the files with `python gen_notebooks.py`. The `.ipynb` ship
without embedded outputs (run them in your Jupyter); the code is tested.
