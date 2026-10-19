# Prototype (Python)

The original desktop / Raspberry Pi prototype that Overhead grew out of. It's kept for reference and experiments; the
[firmware](../firmware) is the real project.

- Live data from adsb.lol or a local readsb receiver, or a built-in **simulator of MSP traffic** on the real runway
  geometry (`--feed sim`, `--flow 30` for departures).
- A browser mock-up of the 8x32 sign with a radar view, plus optional MAX7219 output on a Raspberry Pi.
- `python main.py --analyze` prints altitude, loudness and audible time for the configured location.

```
python main.py --feed sim          # simulated arrivals, mock-up at http://localhost:8080
python main.py                     # live adsb.lol data
```

Set your own location (and anything else in `overhead/config.py`) in a `config.json` next to `main.py`:

```json
{ "home_lat": 44.95, "home_lon": -93.37, "home_elev_ft": 918 }
```

The simulator and the airport flight-board lookup are specific to MSP.
