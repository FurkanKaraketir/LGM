#!/usr/bin/env python3
"""Assemble LGM GitHub Wiki pages from repo docs and rewrite links for wiki context."""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path

DEFAULT_REPO = "FurkanKaraketir/LGM"
DEFAULT_BRANCH = "main"

# Source path (repo-relative) -> wiki page basename (no .md)
SYNCED_PAGES: dict[str, str] = {
    "docs/canvas.md": "Canvas-editor",
    "docs/model.md": "Graph-model",
    "docs/analysis.md": "Analysis",
    "docs/state_space_from_normal_tree.md": "State-space-algorithm",
    "docs/releases.md": "Releases",
    "src/assets/guides/quick_start.md": "Quick-start",
    "src/assets/guides/multiple_normal_tree_finding.md": "Normal-tree-enumeration",
    "src/assets/guides/state_space_from_normal_tree.md": "State-space-derivation",
    "Examples/README.md": "Examples",
}

HANDOUT_PAGES: dict[str, tuple[str, str]] = {
    "docs/LinearGrpahModelingBasics.txt": (
        "Linear-graph-basics",
        "Linear Graph Modeling Basics (MIT 2.151 handout)",
    ),
    "docs/TwoPortsBasics.txt": (
        "Two-ports-basics",
        "Two-Ports Basics (MIT 2.151 handout)",
    ),
}

STATIC_WIKI_PAGES = (
    "Home.md",
    "_Sidebar.md",
    "Building-from-source.md",
    "FAQ.md",
)

# Markdown link targets -> wiki page name
WIKI_LINK_TARGETS: dict[str, str] = {
    "analysis.md": "Analysis",
    "canvas.md": "Canvas-editor",
    "model.md": "Graph-model",
    "state_space_from_normal_tree.md": "State-space-algorithm",
    "releases.md": "Releases",
    "docs/analysis.md": "Analysis",
    "docs/canvas.md": "Canvas-editor",
    "docs/model.md": "Graph-model",
    "docs/state_space_from_normal_tree.md": "State-space-algorithm",
    "docs/releases.md": "Releases",
    "../docs/analysis.md": "Analysis",
    "../docs/canvas.md": "Canvas-editor",
    "../docs/model.md": "Graph-model",
    "../docs/state_space_from_normal_tree.md": "State-space-algorithm",
    "../docs/releases.md": "Releases",
    "Examples/README.md": "Examples",
    "../Examples/README.md": "Examples",
    "Examples/": "Examples",
    "../Examples/": "Examples",
}

EXAMPLE_LGM = re.compile(r"^(?:\.\./)?Examples/([\w.-]+\.lgm)$")
REPO_RELATIVE = re.compile(
    r"^(?:\.\./)*(?:src/|\.github/|Examples/[\w.-]+\.lgm|CMakeLists\.txt|vcpkg\.json)"
)

LINK_RE = re.compile(r"\[([^\]]*)\]\(([^)]+)\)")


def blob_url(repo: str, branch: str, path: str) -> str:
    normalized = path.replace("\\", "/")
    while normalized.startswith("../"):
        normalized = normalized[3:]
    if normalized.startswith("./"):
        normalized = normalized[2:]
    return f"https://github.com/{repo}/blob/{branch}/{normalized}"


def rewrite_link_target(target: str, repo: str, branch: str) -> str:
    if target.startswith(("http://", "https://", "mailto:")):
        return target

    if target in WIKI_LINK_TARGETS:
        return WIKI_LINK_TARGETS[target]

    if EXAMPLE_LGM.match(target):
        return blob_url(repo, branch, target)

    if REPO_RELATIVE.match(target):
        return blob_url(repo, branch, target)

    # Examples/README.md: bare .lgm filenames
    if re.fullmatch(r"[\w.-]+\.lgm", target):
        return blob_url(repo, branch, f"Examples/{target}")

    return target


def rewrite_markdown(content: str, repo: str, branch: str) -> str:
    def replace_link(match: re.Match[str]) -> str:
        text, target = match.group(1), match.group(2)
        new_target = rewrite_link_target(target, repo, branch)
        return f"[{text}]({new_target})"

    return LINK_RE.sub(replace_link, content)


def wrap_handout(path: Path, title: str) -> str:
    body = path.read_text(encoding="utf-8")
    return f"# {title}\n\n```text\n{body}\n```\n"


def sync_wiki(root: Path, out_dir: Path, repo: str, branch: str) -> list[str]:
    wiki_src = root / "wiki"
    written: list[str] = []

    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)

    for src_rel, page_name in SYNCED_PAGES.items():
        src = root / src_rel
        if not src.is_file():
            raise FileNotFoundError(src)
        content = rewrite_markdown(src.read_text(encoding="utf-8"), repo, branch)
        dest = out_dir / f"{page_name}.md"
        dest.write_text(content, encoding="utf-8", newline="\n")
        written.append(page_name)

    for src_rel, (page_name, title) in HANDOUT_PAGES.items():
        src = root / src_rel
        if not src.is_file():
            raise FileNotFoundError(src)
        dest = out_dir / f"{page_name}.md"
        dest.write_text(wrap_handout(src, title), encoding="utf-8", newline="\n")
        written.append(page_name)

    for name in STATIC_WIKI_PAGES:
        src = wiki_src / name
        if not src.is_file():
            raise FileNotFoundError(src)
        shutil.copy2(src, out_dir / name)
        written.append(name.removesuffix(".md"))

    return written


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        required=True,
        help="Output directory for wiki markdown files",
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="Repository root (default: parent of scripts/)",
    )
    parser.add_argument("--repo", default=DEFAULT_REPO, help="GitHub owner/repo")
    parser.add_argument("--branch", default=DEFAULT_BRANCH, help="Git branch for blob links")
    args = parser.parse_args()

    pages = sync_wiki(args.root, args.output, args.repo, args.branch)
    print(f"Wrote {len(pages)} wiki pages to {args.output}")
    for name in sorted(pages):
        print(f"  - {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
