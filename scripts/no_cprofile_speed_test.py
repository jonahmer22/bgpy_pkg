import cProfile
import io
import pstats
import time
from pathlib import Path

from bgpy.shared.enums import SpecialPercentAdoptions
from bgpy.simulation_engine import ROV
from bgpy.simulation_framework import (
    ScenarioConfig,
    Simulation,
    SubprefixHijack,
)


def main():
    """Runs the defaults"""

    # Simulation for the paper
    sim = Simulation(
        percent_adoptions=(
            SpecialPercentAdoptions.ONLY_ONE,
            0.1,
            0.2,
            0.5,
            0.8,
            0.99,
            # Using only 1 AS not adopting causes extreme variance
            # SpecialPercentAdoptions.ALL_BUT_ONE,
        ),
        scenario_configs=(
            ScenarioConfig(ScenarioCls=SubprefixHijack, AdoptPolicyCls=ROV),
        ),
        output_dir=Path("~/Desktop/speed_test").expanduser(),
        num_trials=10,
        parse_cpus=1,
    )
    sim.run(GraphFactoryCls=None)


if __name__ == "__main__":
    start = time.perf_counter()
    main()
    print(f"{time.perf_counter() - start:.2f}s")
