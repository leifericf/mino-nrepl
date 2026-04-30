# Changelog

All notable changes to mino-nrepl are documented here.

## Unreleased

- Tracking mino v0.97.5 (Kwargs + Audit + Hygiene cycle: kwargs
  destructuring matches Clojure 1.11 (inline pairs, trailing map,
  mixed; `:or` defaults eval correctly); `iteration` rewritten to
  canon `& {:keys [...]}` shape; `sort-by` and `reductions` gain
  multi-arity; `src/core.clj` 80-char wrap; `defn` lifted so six
  bootstrap `def + fn` forms become `defn`; `clojure.core.async`
  gains canon `reduce` / `transduce` / `split` / `partition-by`;
  `clojure.spec.alpha` gains `abbrev` / `describe`). No nREPL-side
  changes — the bencode encoder serialises printed forms via mino's
  printer; the new fns surface through eval and op completion via
  the existing routing. Builds clean against the bumped submodule.
- Tracking mino v0.96.8 (Canon-Parity cycle: real `MINO_VOLATILE`
  primitive, stateful-transducer rewrites, lazy-seq recur-on-skip,
  transient reductions, comp/partial/some-fn/every-pred unrolling
  plus `into` 0/1-arg and `unchecked-divide-int`, `iteration` from
  Clojure 1.11, `clojure.core.async` namespace wrap with `merge`/`into`
  renames, the `:refer :all` transitive-drag fix, and the chunked-seq
  family with two new value types and eight primitives). No nREPL-side
  changes — the bencode encoder serialises printed forms via mino's
  own printer, and the new value types print under the existing
  routing; the op dispatcher and wire format are unchanged. Builds
  clean against the bumped submodule.
- Tracking mino v0.95.5 (Clojure-side hygiene pass: bundled stdlib
  refactor). No nREPL-side changes — the hygiene pass is internal
  to the mino-side library; the bencode wire format and op
  dispatcher are unchanged.
- Tracking mino v0.94.0 (empty-list canon parity). No nREPL-side
  changes; the bencode encoder operates on its own type tags, not
  `mino_val_t`, so the new `MINO_EMPTY_LIST` enum entry has no effect
  on the wire format.
- Tracking mino v0.93.0 (C refactoring pass; bundled `mino deps` and
  `mino task` tooling; bootstrap Makefile). The Makefile gains three
  gen-mino-header entries for the new `lib/mino/*` sources that v0.93.0
  bakes into the binary. No nREPL-side changes; eval and completion
  ops continue to surface new bindings automatically.
- Tracking mino v0.74.0 (deferred core surface): `*ns*` as a real var,
  `bound-fn` / `bound-fn*`, `read` with options, `clojure.edn/read`,
  `destructure`, and regex capture groups with `re-matcher` /
  `re-groups`. No nREPL-side changes needed; eval and completion ops
  surface the new bindings through the existing session env.
- Tracking mino v0.73.0 (first-class namespaces): each namespace owns
  its own root binding table, vars are first-class objects, auto-
  resolved keywords and namespaced map literals land at read time,
  and source files use `.clj` instead of `.mino`. Makefile picks up
  the new `runtime/ns_env.c` and `runtime/path_buf.c` TUs.
- Tracking mino v0.48.0: character type, sorted collections, first-class
  vars, transients, and the structured exception + argument-parsing
  helpers in `public_embed.c`. Makefile extended to compile the new
  `transient.c` and `public_embed.c` TUs.
- Tracking mino v0.42.0: generational + incremental garbage collector,
  public GC API, literal-builder barrier fix. Makefile extended to
  compile the new `runtime_gc_roots.c`, `runtime_gc_major.c`,
  `runtime_gc_barrier.c`, `runtime_gc_minor.c`, `public_gc.c`, and
  `prim_lazy.c` TUs.
- Tracking mino v0.39.1 (task runner, `str-replace` primitive,
  `file-mtime` primitive, Windows CI)
