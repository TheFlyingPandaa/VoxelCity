# Density zones and road hierarchy

Residential and commercial zoning each provide low- and high-density tools. Both densities share the existing residential or commercial demand and tax rate. High-density parcels are available immediately, have three times the per-level capacity, use proportionally more utilities and services, generate more trips, and develop into taller procedural voxel buildings. Industrial zoning is unchanged.

The city road menu provides:

| Road | Lanes | Speed | Cost per step | Upkeep per minute | Frontage |
|---|---:|---:|---:|---:|---|
| Two-lane street | 1 each way | 16 | $20 | $0.01 | Yes |
| Two-lane one-way | 2 forward | 20 | $25 | $0.015 | Yes |
| Four-lane avenue | 2 each way | 24 | $60 | $0.03 | Outer edges |
| Four-lane highway | 2 each way | 32 | $100 | $0.04 | No |
| Six-lane highway | 3 each way | 36 | $140 | $0.06 | No |

Avenues reserve two road tiles across. Divided highways reserve two carriageways and a non-routable median across three tiles. Drag direction sets one-way travel and opposing carriageway directions. Compatible construction upgrades in place and charges the positive cost difference; a blocked wider footprint rejects the complete stroke.

Highways do not connect directly to streets or provide building access. Place the 9 x 9 diamond-interchange tool on the median of a straight divided highway to add the fixed local-road connection. Region-owned highway cells remain protected.

Density and road hierarchy were introduced in city save version 6 with traffic payload version 4. Current city saves use version 7, adding [railway state](RAILWAY.md), and retain traffic payload version 4. City versions 3-6 remain readable; version 1/2 road maps import into empty cities. For pre-v6 cities: legacy residential and commercial parcels become low density, local roads become two-lane streets, and existing regional highway geometry is retained.
