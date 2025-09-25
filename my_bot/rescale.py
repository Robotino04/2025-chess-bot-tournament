import re


def parse_results(text):
    players = []
    in_table = False
    for line in text.splitlines():
        if line.strip().startswith("Rank"):
            in_table = True
            continue
        if in_table and line.strip() == "":
            break
        if in_table:
            match = re.match(
                r"\s*\d+\s+(.+?)\s+(-?\d+)\s+(\d+)\s+(\d+)\s+([\d.]+%)\s+([\d.]+%)",
                line,
            )
            if match:
                name = match.group(1).strip()
                elo = int(match.group(2))
                players.append((name, elo))
    return players


def rescale_elos_two_points(
    players, ref1_name, ref1_target_elo, ref2_name, ref2_target_elo
):
    ref1 = next((p for p in players if p[0] == ref1_name), None)
    ref2 = next((p for p in players if p[0] == ref2_name), None)
    if not ref1 or not ref2:
        raise ValueError("One or both reference engines not found.")

    r1, r1p = ref1[1], ref1_target_elo
    r2, r2p = ref2[1], ref2_target_elo

    if r1 == r2:
        raise ValueError("Reference engines must have different tournament Elos.")

    # Linear scale: R' = a * R + b
    a = (r2p - r1p) / (r2 - r1)
    b = r1p - a * r1

    scaled = []
    for name, elo in players:
        corrected = round(a * elo + b)
        scaled.append((name, elo, corrected))
    return scaled


def print_results(
    scaled_players, ref1_name, ref1_target_elo, ref2_name, ref2_target_elo
):
    print(
        f"\nCorrected Elos (using anchors: '{ref1_name}' = {ref1_target_elo}, '{ref2_name}' = {ref2_target_elo}):\n"
    )
    print(f"{'Engine':30} {'Raw Elo':>10} {'Corrected Elo':>15}")
    print("-" * 60)
    for name, raw, corrected in scaled_players:
        print(f"{name:30} {raw:10} {corrected:15}")


# Example usage
if __name__ == "__main__":
    # Paste your tournament result here as a multiline string
    result_text = """
Rank Name                          Elo     +/-   Games   Score    Draw 
   1 Stockfish 1500                452      42     900   93.1%    3.3% 
   2 Stockfish 1320                227      26     900   78.7%    7.3% 
   3 NB Latest                      11      18     900   51.6%   34.2% 
   4 NB v3_no_rechecks             -68      19     900   40.4%   29.2% 
   5 NB v4_transpos               -120      20     900   33.4%   24.4% 
   6 NB v5_good_transpos          -140      22     900   30.8%   16.8% 
   7 NB v2_no_additional_check    -220      18     900   22.0%   39.1%
    """

    ref1 = "Stockfish 1320"
    ref1_target = 1320

    ref2 = "Stockfish 1500"
    ref2_target = 1500

    players = parse_results(result_text)
    scaled_players = rescale_elos_two_points(
        players, ref1, ref1_target, ref2, ref2_target
    )
    print_results(scaled_players, ref1, ref1_target, ref2, ref2_target)
