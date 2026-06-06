# notebooks/ — reproducible experiments

They generate the charts from real measurements (compiling small C harnesses with
the header, or running the model). They need `gcc`, `matplotlib` and `numpy`.

| Notebook | Measures |
|---|---|
| `01_cache_cliff.ipynb`   | cache cliff: `max` speedup (u8 vs int64) vs N |
| `02_pfor.ipynb`          | PFOR memory recovery vs outlier fraction |
| `03_zonemap.ipynb`       | free zone-map: % of columns/bands skipped (model) |
| `04_ai_blocked.ipynb`    | AI: per-channel vs per-token memory vs outlier fraction |
| `05_capacity_model.ipynb`| server capacity model (estimate) |

The harnesses locate the header by walking up from `notebooks/` until they find
`include/`. Regenerate the files: `python gen_notebooks.py`. The `.ipynb` come
without embedded outputs (run them in your Jupyter); the code has been tested.
