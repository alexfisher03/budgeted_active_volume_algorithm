#!/usr/bin/env python3
"""Render AIG predicate DAGs as publication-quality SVG/PNG diagrams."""

import json
import subprocess
import argparse
from pathlib import Path

PALETTE = {
    "input_fill":  "#e8eef4",
    "input_border": "#5a7da0",
    "gate_fill":   "#fafafa",
    "gate_border": "#555555",
    "root_fill":   "#fff0e0",
    "root_border": "#c07030",
    "edge":        "#333333",
    "neg_edge":    "#333333",
    "bg":          "white",
    "title":       "#222222",
    "label":       "#222222",
}


def load_aig(path):
    with open(path) as f:
        return json.load(f)


def positive_base(lit):
    return lit & ~1


def is_negated(lit):
    return (lit & 1) != 0


def node_id_from_lit(lit):
    return lit >> 1


def format_title(export_id):
    return export_id.replace("_", " ")


def format_input_label(ann):
    sig = ann["signal"]
    bit = ann["bit"]
    time = ann["time"]
    if sig == "rst_n":
        base = f"rst\u0305[{bit}]"
    else:
        base = f"{sig}[{bit}]"
    if time > 0:
        return f"{base} @ t{time}"
    return base


def render_aig_dot(aig):
    export_id = aig["export_id"]
    root_lit = aig["root_lit"]
    input_lits = set(aig["inputs"])
    node_count = aig["node_count_cone"]
    nodes = aig["nodes"]

    input_labels = {}
    for ann in aig["input_annotations"]:
        input_labels[ann["lit"]] = format_input_label(ann)

    root_nid = node_id_from_lit(positive_base(root_lit))
    node_ids = {n["id"] for n in nodes}
    is_small = node_count <= 20

    title = format_title(export_id)
    font = "Helvetica"

    L = []
    L.append("digraph AIG {")
    L.append(f'  label=<'
             f'<font face="{font}" point-size="16" color="{PALETTE["title"]}">'
             f'<b>{title}</b></font>'
             f'<br/><br/>'
             f'<font face="{font}" point-size="10" color="#666666">'
             f'{len(input_lits)} inputs &middot; {node_count} AND gates'
             f'</font><br/>>;')
    L.append("  labelloc=t;")
    L.append(f'  fontname="{font}";')
    L.append("  rankdir=TB;")
    L.append(f'  node [fontname="{font}"];')
    L.append(f'  edge [fontname="{font}"];')
    L.append("  splines=true;")
    L.append(f"  bgcolor={PALETTE['bg']};")
    L.append("  pad=0.4;")
    L.append("  margin=0.2;")

    if is_small:
        L.append("  nodesep=0.4;")
        L.append("  ranksep=0.55;")
    else:
        L.append("  nodesep=0.25;")
        L.append("  ranksep=0.42;")
    L.append("")

    ns = 8 if is_small else 6.5
    gate_w = "0.34" if is_small else "0.28"

    L.append("  subgraph cluster_inputs {")
    L.append("    rank=source;")
    L.append("    style=invis;")
    for lit in sorted(input_lits):
        nid = node_id_from_lit(lit)
        label = input_labels.get(lit, f"x{nid}")
        L.append(f'    in_{nid} [label="{label}", shape=box, '
                 f'style="filled,rounded", '
                 f'fillcolor="{PALETTE["input_fill"]}", '
                 f'color="{PALETTE["input_border"]}", '
                 f'fontsize={ns}, fontcolor="{PALETTE["label"]}", '
                 f'penwidth=0.8, '
                 f'width=0.6, height=0.22, margin="0.06,0.03"];')
    L.append("  }")
    L.append("")

    for n in nodes:
        nid = n["id"]
        if nid == root_nid:
            L.append(f'  n_{nid} [label="{nid}", shape=circle, '
                     f'style=filled, '
                     f'fillcolor="{PALETTE["root_fill"]}", '
                     f'color="{PALETTE["root_border"]}", '
                     f'fontsize={ns}, fontcolor="{PALETTE["label"]}", '
                     f'penwidth=1.6, '
                     f'width={gate_w}, height={gate_w}, fixedsize=true];')
        else:
            L.append(f'  n_{nid} [label="{nid}", shape=circle, '
                     f'style=filled, '
                     f'fillcolor="{PALETTE["gate_fill"]}", '
                     f'color="{PALETTE["gate_border"]}", '
                     f'fontsize={ns}, fontcolor="{PALETTE["label"]}", '
                     f'penwidth=0.8, '
                     f'width={gate_w}, height={gate_w}, fixedsize=true];')
    L.append("")

    pw = "0.6" if is_small else "0.45"
    arrow = "normal"

    for n in nodes:
        nid = n["id"]
        for fanin_lit in (n["lhs"], n["rhs"]):
            if fanin_lit <= 1:
                const_label = "0" if fanin_lit == 0 else "1"
                const_id = f"const_{fanin_lit}_{nid}"
                L.append(f'  {const_id} [label="{const_label}", '
                         f'shape=plaintext, fontsize={ns}];')
                L.append(f"  {const_id} -> n_{nid} "
                         f'[penwidth={pw}, arrowsize=0.5, '
                         f'color="{PALETTE["edge"]}"];')
                continue

            base = positive_base(fanin_lit)
            neg = is_negated(fanin_lit)

            if base in input_lits:
                src = f"in_{node_id_from_lit(base)}"
            else:
                src_nid = node_id_from_lit(base)
                if src_nid in node_ids:
                    src = f"n_{src_nid}"
                else:
                    src = f"unk_{src_nid}"
                    L.append(f'  {src} [label="?", shape=plaintext];')

            attrs = [
                f"penwidth={pw}",
                f"arrowsize=0.5",
                f'color="{PALETTE["neg_edge" if neg else "edge"]}"',
            ]
            if neg:
                attrs.append("arrowhead=odot")
                attrs.append("style=dashed")
            else:
                attrs.append(f"arrowhead={arrow}")

            L.append(f"  {src} -> n_{nid} [{', '.join(attrs)}];")

    L.append("")

    neg_root = is_negated(root_lit)
    root_edge_attrs = [
        f"penwidth={pw}",
        "arrowsize=0.5",
        f'color="{PALETTE["root_border"]}"',
    ]
    if neg_root:
        root_edge_attrs.append("arrowhead=odot")
        root_edge_attrs.append("style=dashed")

    L.append(f'  output [label="output", shape=plaintext, '
             f'fontsize={ns + 1}, fontcolor="{PALETTE["root_border"]}", '
             f'fontname="{font}"];')
    L.append(f"  n_{root_nid} -> output [{', '.join(root_edge_attrs)}];")

    L.append("}")
    return "\n".join(L)


def render_pdf(dot_path, pdf_path):
    subprocess.run(
        ["dot", "-Tpdf", str(dot_path), "-o", str(pdf_path)],
        check=True)


def main():
    parser = argparse.ArgumentParser(
        description="Render AIG predicate DAGs as SVG and PNG diagrams.")
    parser.add_argument("json_paths", nargs="+",
                        help="Paths to predicate JSON files")
    parser.add_argument("--outdir", default="plotting/output",
                        help="Output directory (default: plotting/output)")
    args = parser.parse_args()

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    for json_path in args.json_paths:
        aig = load_aig(json_path)
        dot_src = render_aig_dot(aig)
        stem = Path(json_path).stem

        dot_path = outdir / f"{stem}_aig.dot"
        with open(dot_path, "w") as f:
            f.write(dot_src)

        for fmt in ("svg", "png", "pdf"):
            out_path = outdir / f"{stem}_aig.{fmt}"
            extra = ["-Gdpi=300"] if fmt == "png" else []
            subprocess.run(
                ["dot", f"-T{fmt}", *extra, str(dot_path), "-o", str(out_path)],
                check=True)
            print(f"  {out_path}")

        print(f"Rendered {stem} ({aig['node_count_cone']} nodes)")


if __name__ == "__main__":
    main()
