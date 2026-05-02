import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from pathlib import Path

from patches.TombRaiderLegend.nightly import executor as executor_mod
from patches.TombRaiderLegend.nightly import orchestrator as orchestrator_mod
from patches.TombRaiderLegend.nightly.model import CandidateSpec


class _FakeLedger:
    def __init__(self) -> None:
        self.runs = {}
        self.publications = []
        self.autopatch = {
            "iterations": [],
            "confirmed_patches": [],
            "blacklisted_addrs": [],
            "diagnostic_results": [],
            "tried_addrs": [],
        }

    def autopatch_section(self):
        return self.autopatch

    def upsert_run(self, state):
        self.runs[state.run_id] = state

    def get_run(self, run_id):
        return self.runs.get(run_id)

    def record_publication(self, run_id, payload):
        self.publications.append((run_id, payload))


def test_nightly_dry_run_exercises_full_loop(tmp_path: Path, monkeypatch) -> None:
    fake_ledger = _FakeLedger()
    monkeypatch.setattr(orchestrator_mod, "RUNS_ROOT", tmp_path / "runs")
    monkeypatch.setattr(orchestrator_mod, "WORKTREES_ROOT", tmp_path / "worktrees")
    monkeypatch.setattr(executor_mod, "WORKTREES_ROOT", tmp_path / "worktrees")
    monkeypatch.setattr(orchestrator_mod.ExperimentLedger, "load", classmethod(lambda cls: fake_ledger))

    monkeypatch.setattr(
        orchestrator_mod,
        "generate_initial_candidate_specs",
        lambda config, ledger: [
            CandidateSpec(
                candidate_id="cfg-sky-wide",
                mutation_class="config_only",
                description="sky-first",
                proxy_overrides={"Sky": {"CandidateMinVerts": 8000}},
            ),
            CandidateSpec(
                candidate_id="cfg-water-tag",
                mutation_class="config_only",
                description="water-first",
                rtx_overrides={"rtx.translucentMaterial.animatedWaterEnable": True},
            ),
        ],
    )
    monkeypatch.setattr(
        orchestrator_mod,
        "generate_source_candidate_specs",
        lambda config, parents, round_index, existing_ids: (
            [CandidateSpec(
                candidate_id="cfg-sky-wide-water_animation_preserve-r1",
                mutation_class="source_mutation",
                description="water animation preserve",
                parent_candidate_id="cfg-sky-wide",
                source_template="water_animation_preserve",
                round_index=1,
            )]
            if round_index == 0 else []
        ),
    )

    orchestrator = orchestrator_mod.NightlyOrchestrator(dry_run=True)
    monkeypatch.setattr(orchestrator, "bootstrap", lambda scene_ids=None: {"ok": True})

    state = orchestrator.run(hours=1)
    payload = orchestrator.publish(state.run_id)

    assert state.status == "completed"
    assert state.phase == "screened"
    assert state.screen_winner_id == "cfg-sky-wide-water_animation_preserve-r1"
    assert state.winner_id is None
    assert payload["screen_winner_id"] == state.screen_winner_id
    assert payload["winner_id"] is None
    assert payload["run_branch"].startswith("nightly/trl-")
    assert Path(payload["payload_path"]).exists()
