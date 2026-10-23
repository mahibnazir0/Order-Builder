# Milestone 1 restatement

The Milestone 1 totals were reconciled with the client and signed off. Since then, three rulings have changed how a demand line is weighed or converted to pallet-equivalents. Each change moves the M1 totals, so this page restates them and keeps the signed-off figures for comparison. The signed-off 17 August report is unchanged in `M1_Output_Final.txt` and `M1_Output_Revised.txt`. The same day under the current rules is in `m1OutputRestated.txt`.

## What changed, and when

| Date | Ruling | Effect on M1 |
|---|---|---|
| 13 Oct 2026 | Weigh pallets from the item master ("Use what is in the item master"). Wood pallets weigh 65 lb, not the confirmed 60 lb; TLD adds 1 lb and GMA 0.1 lb. | Weight only, up about 0.35%. Pallet-equivalents unchanged. |
| 20 Oct 2026 | DIS quantities are cases ("DIS turns out to be less than a pallet"): divided by Cases_Unit_Load. | Pallet-equivalents and weight down 3–6%. One or two fewer warnings per day (lines that no longer cross the 300-pallet review threshold). |
| 24 Oct 2026 (pending the client's agreement) | A PAL quantity counts the product's base unit and is divided by Cases_Unit_Load, as CS and DIS are. This only changes PAL products with Cases_Unit_Load above 1, where two PAL pieces stack into one unit load. | Pallet-equivalents and weight down about another 1.5%. |

Why the 24 October change: on every extract, every demand line's unit of measure matches its product's unit in the master, so the quantity counts the master's base unit. The client's 16 October example, 18 DIS per unit load, works the same way. Reading PAL as whole unit loads makes two of Truck Builder's August loads heavier than the 45,000 lb limit; the heaviest would be 52,015 lb. Dividing by Cases_Unit_Load leaves none over, with the heaviest at 42,974 lb. For example, load 1344793 carries 60 PAL of 104578600, where one PAL is a 421.4 lb, 49.375 in piece and two make a unit load. Read as unit loads, that's 50,568 lb of cargo and 60 single-high 98.75 in stacks on one trailer. Read as base units, it's 30 stacks and 25,284 lb, which fits.

## 17 August and the September extracts (reconciled 26 Sep 2026)

Pallet-equivalents:

| Extract | Signed off | After 13 Oct | After 20 Oct | After 24 Oct |
|---|---:|---:|---:|---:|
| 17 Aug | 152,911.2 | 152,911.2 | 147,671.3 | 145,705.8 |
| 2 Sep #1 | 149,391.3 | 149,391.3 | 144,551.6 | 142,075.6 |
| 2 Sep #2 | 146,023.0 | 146,023.0 | 141,338.1 | 139,152.1 |
| 3 Sep | 139,678.5 | 139,678.5 | 134,618.9 | 132,704.9 |

Total weight (lb):

| Extract | Signed off | After 13 Oct | After 20 Oct | After 24 Oct |
|---|---:|---:|---:|---:|
| 17 Aug | 103,005,833 | 103,367,993 | 100,334,597 | 99,014,698 |
| 2 Sep #1 | 98,793,082 | 99,155,309 | 96,435,098 | 94,730,452 |
| 2 Sep #2 | 95,668,859 | 96,023,693 | 93,321,776 | 91,900,732 |
| 3 Sep | 91,591,555 | 91,924,765 | 88,949,381 | 87,634,627 |

Validation warnings (excluding Milestone 2's own-CRI warnings) went from 161 / 204 / 207 / 169 to 158 / 200 / 206 / 168.

## 29 September to 5 October extracts (measured 10 Oct 2026)

| Extract | Pallet-equivalents, 10 Oct | Now | Weight (lb), 10 Oct | Now |
|---|---:|---:|---:|---:|
| 29 Sep | 141,697.0 | 132,011.8 | 94,052,648 | 88,702,752 |
| 30 Sep | 143,118.8 | 132,703.1 | 95,418,783 | 89,611,591 |
| 1 Oct | 133,623.2 | 121,695.7 | 88,943,337 | 82,092,973 |
| 2 Oct | 123,367.0 | 111,013.1 | 81,696,332 | 74,485,410 |
| 5 Oct | 148,004.3 | 132,781.5 | 99,787,983 | 90,786,471 |

## Lines each change touches

| Extract | DIS lines (Cases_Unit_Load not 1) | PAL lines (Cases_Unit_Load above 1) |
|---|---:|---:|
| 17 Aug | 120 | 242 |
| 2 Sep #1 | 109 | 282 |
| 2 Sep #2 | 97 | 266 |
| 3 Sep | 101 | 242 |
| 29 Sep | 147 | 224 |
| 30 Sep | 151 | 216 |
| 1 Oct | 159 | 222 |
| 2 Oct | 167 | 203 |
| 5 Oct | 183 | 217 |

The demand line counts, hash totals, lane counts and placeholder figures are unchanged on every extract. The tests pin the current figures in `tests/importer/crossDayFixtures.hpp`.
