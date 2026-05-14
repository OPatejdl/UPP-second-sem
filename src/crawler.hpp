#pragma once

// Spustí řídicí uzel — HTTP server, distribuce URL, sběr výsledků.
// n - počet Worker A uzlů
// m - počet Worker B uzlů na každý Worker A
void run_master(int n, int m);

// Spustí Worker A uzel — správa crawlingu jedné domény pomocí Worker B uzlů.
// rank - MPI rank tohoto procesu
// n    - celkový počet Worker A uzlů
// m    - počet Worker B uzlů na každý Worker A
void run_worker_a(int rank, int n, int m);

// Spustí Worker B uzel — analýza jedné stránky na žádost Worker A.
// rank - MPI rank tohoto procesu
// n    - celkový počet Worker A uzlů
// m    - počet Worker B uzlů na každý Worker A
void run_worker_b(int rank, int n, int m);
