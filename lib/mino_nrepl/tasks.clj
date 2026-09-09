(ns mino-nrepl.tasks)

;; mino-nrepl's task runner. The build consumes the mino amalgamation
;; (mino/dist/mino.{c,h}), the single embedding boundary mino ships.
;; Every nREPL source compiles against dist/mino.o and includes
;; dist/mino.h only; nothing reaches into mino's private src/** tree,
;; so a mino C-tree reorg is a no-op for this build once the submodule
;; pin is bumped.
;;
;; How the amalgam is materialized is not our recipe: mino ships it as
;; mino.tasks.amalgam, required below and resolved from mino/lib on
;; :paths. A change to the amalgam machinery reaches this build through
;; the submodule; there is no local copy to drift (ADR 62).
;;
;; First-time bootstrap: `cd mino && make && cd ..`. After that, every
;; rebuild goes through `./mino/mino task build`, which regenerates the
;; amalgamation only when the submodule pin changes.

(require '[clojure.string :as str])
(require '[mino.tasks.amalgam :as amalgam])

;;;; Build configuration

(def ^:private cc (or (getenv "CC") "cc"))

;; The amalgamation is the sole mino include root: dist/mino.h is the
;; only mino header any nREPL source includes, so -Imino/dist rides on
;; cflags for both the amalgam compile and every nREPL source. -Isrc
;; (added at the build site) picks up the local bencode/session/ops
;; headers.
(def ^:private cflags
  (str/split (or (getenv "CFLAGS")
                 "-std=c99 -Wall -Wpedantic -Wextra -O2 -Imino/dist")
             #" "))

(def ^:private libs
  (str/split (or (getenv "LIBS") "-lm -lpthread") #" "))

(def ^:private target "mino-nrepl")

;; The nREPL sources, each linked against the amalgam object.
(def ^:private srcs
  ["src/main.c" "src/bencode.c" "src/session.c" "src/ops.c"])

;;;; Build

(defn build
  "Build the mino-nrepl binary against the mino amalgamation."
  []
  (amalgam/ensure-dist! cc cflags)
  (if (amalgam/stale? (concat srcs [amalgam/dist-obj]) target)
    (let [args (into [cc] (concat cflags
                                  ["-Isrc" "-o" target]
                                  srcs [amalgam/dist-obj] libs))]
      (println (str "  " (str/join " " args)))
      (apply sh! args)
      (println (str "  built " target)))
    (println (str "  " target " up to date"))))

;;;; Test

(defn test
  "Run the nREPL protocol test suite (tests/test_nrepl.sh)."
  []
  (let [r (sh "sh" "-c" "tests/test_nrepl.sh")]
    (println (:out r))
    (when-not (str/blank? (str (:err r)))
      (println (:err r)))
    (when-not (zero? (:exit r))
      (throw (ex-info "nREPL tests failed" {:exit (:exit r)})))))

;;;; Clean

(defn clean
  "Remove the mino-nrepl binary (never touches the mino/ submodule)."
  []
  (when (file-exists? target) (rm-rf target))
  (println "  cleaned"))
