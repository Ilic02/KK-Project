# Liveness analiza i Dead Store Elimination — prezentacija

## 1. Šta je dead store elimination?

**Dead store** je `store` instrukcija koja upisuje vrednost koja se **nikad ne pročita** — pre nego što se eventualno prepiše drugim store-om, ili dok program ne završi tu putanju izvršavanja. Takav store je beskoristan i može se bezbedno obrisati bez promene ponašanja programa.

```llvm
store i32 1, ptr %p   ; mrtav — nikad se ne čita
store i32 2, ptr %p   ; ostaje — ovo se čita ispod
%x = load i32, ptr %p
```

Da bismo znali **koji** store je mrtav, moramo znati: *za svaku tačku u programu, koje vrednosti će se kasnije koristiti?* Ovo pitanje rešava **liveness analiza**.

---

## 2. Liveness — osnovni pojmovi

Za pointer (memorijsku lokaciju) kažemo da je **live** (živ) u nekoj tački programa ako postoji **bar jedna putanja** izvršavanja od te tačke na kojoj će se vrednost na tom pointeru **pročitati** (load) pre nego što se **prepiše** (store).

Za basic block `BB` definišemo dva skupa:

| Skup | Značenje |
|---|---|
| `LiveIn[BB]` | Pointeri čija je vrednost živa **pre** ulaska u `BB`. |
| `LiveOut[BB]` | Pointeri čija je vrednost, živa **na izlasku** iz `BB`. |

**Pravilo unutar bloka** (instrukcija po instrukciju):

- **`load %p`** → `%p` postaje *live* (vrednost je potrebna u ovoj tački).
- **`store %p, v`** → `%p` postaje *dead* (vrednost se prepisuje; sve što je bilo potrebno *posle* ove tačke je "zadovoljeno" ovim store-om, ne mora dalje da se prati unazad).

---

## 3. Zašto se ide UNAZAD kroz instrukcije?

Liveness je **backward** (povratna) data-flow analiza. Da bismo znali da li je pointer live *pre* neke instrukcije, mora nam biti poznato šta se desi *posle* nje — ta informacija "dolazi iz budućnosti" teksta koda. Idući unazad (od zadnje instrukcije ka prvoj), u svakom trenutku skup `Live` već sadrži tačnu informaciju o svemu što dolazi iza trenutne tačke, jer je to već obrađeno u prethodnim korenicima petlje.

### Primer — zašto je redosled bitan

```llvm
store i32 1, ptr %p   ; (1)
%x = load i32, ptr %p ; (2)
store i32 2, ptr %p   ; (3)
```

Idemo **unazad**: (3) → (2) → (1).

| Korak | Instr | Akcija | Live posle |
|---|---|---|---|
| start | — | — | `{}` (LiveOut) |
| 1 | (3) `store %p,2` | erase(%p) — no-op, nije bio u skupu | `{}` |
| 2 | (2) `load %p` | insert(%p) | `{%p}` |
| 3 | (1) `store %p,1` | `%p` JE u skupu → erase(%p) | `{}` |

`LiveIn = {}`. I to je tačno: load (2) čita vrednost koju je upisao store (1) — ali store (3), koji dolazi **posle** load-a, prepisuje vrednost koju niko više ne čita, pa je mrtav. Spoljna vrednost `%p` (ona pre ovog bloka) se nikad ne koristi.

**Ključan trenutak:** kada store (3) "obriše" `%p` iz `Live`, on kaže: *"moja vrednost rešava sve potrebe za čitanjem koje dolaze posle mene — sve pre mene ne mora da brine o njima."* Zato store **gasi** (kills) liveness, a load ga **pali** (generates).

---

## 4. Zašto fixpoint iteracija preko CFG-a?

Jedan blok zavisi od svojih sledbenika:

$$
\text{LiveOut}(BB) = \bigcup_{Succ \,\in\, \text{successors}(BB)} \text{LiveIn}(Succ)
$$

`successors(BB)` vraća samo **direktne** sledbenike (one na koje `BB` grana svojim terminatorom), ne sve tranzitivno dostupne blokove.

Problem: ako CFG ima **petlju** (ciklus), npr. `A → B → A`, onda `A` zavisi od `B`, a `B` zavisi od `A` — kružna zavisnost. Ne postoji redosled obrade bez "pogađanja". Rešenje: **fixpoint iteracija**:

1. Inicijalizuj sve `LiveIn`/`LiveOut` na `{}` (praznо je korektna polazna tačka za "may" analize — liveness raste monotono ka tačnom rešenju).
2. Prolazi kroz sve blokove, iznova računajući `LiveIn`/`LiveOut` na osnovu **trenutnih** (možda još nepotpunih) vrednosti.
3. Ponavljaj dok se *bar nešto* menja.
4. Kada jedna kompletna runda ne promeni ništa → **fixpoint** dostignut, vrednosti su konačne i tačne.

Broj rundi zavisi od redosleda obrade blokova i strukture CFG-a, ali **konačan rezultat je uvek isti**, nezavisno od redosleda.

---

## 5. Pass — pregled kroz kod

### 5.1 Stanje pass-a

```cpp
using PtrSet = std::unordered_set<Value *>;
std::unordered_map<BasicBlock *, PtrSet> LiveIn;
std::unordered_map<BasicBlock *, PtrSet> LiveOut;
```

Mape koje za svaki basic block čuvaju njegov `LiveIn`/`LiveOut` skup pointera (poredi se po `Value*`, tj. po sintaksičkom identitetu pointer-operanda — **bez** alias analize, što je važno ograničenje, videti sekciju 7).

### 5.2 `computeLiveIn` — lokalna analiza unutar jednog bloka

```cpp
PtrSet computeLiveIn(BasicBlock &BB, const PtrSet &LiveOutSet) {
    PtrSet Live = LiveOutSet;
    for (auto It = BB.rbegin(); It != BB.rend(); ++It) {
      Instruction &Instr = *It;
      if (StoreInst *SI = dyn_cast<StoreInst>(&Instr)) {
        Live.erase(SI->getPointerOperand());
      } else if (LoadInst *LI = dyn_cast<LoadInst>(&Instr)) {
        Live.insert(LI->getPointerOperand());
      }
    }
    return Live;
}
```

- Kreće od `LiveOutSet` (šta je potrebno na izlasku).
- Ide **unazad** kroz instrukcije (`rbegin`/`rend`).
- `store` → `erase` (gasi potrebu).
- `load` → `insert` (pali potrebu).
- Rezultat na kraju petlje = `LiveIn[BB]`.

### 5.3 `computeLiveness` — fixpoint preko cele funkcije

```cpp
void computeLiveness(Function &F) {
    for (BasicBlock &BB : F) {
      LiveIn[&BB] = PtrSet();
      LiveOut[&BB] = PtrSet();
    }
    bool Changed;
    do {
      Changed = false;
      for (BasicBlock &BB : F) {
        PtrSet NewLiveOut;
        for (BasicBlock *Succ : successors(&BB)) {
          const PtrSet &SuccLiveIn = LiveIn[Succ];
          NewLiveOut.insert(SuccLiveIn.begin(), SuccLiveIn.end());
        }
        PtrSet NewLiveIn = computeLiveIn(BB, NewLiveOut);
        if (NewLiveOut != LiveOut[&BB] || NewLiveIn != LiveIn[&BB]) {
          LiveOut[&BB] = std::move(NewLiveOut);
          LiveIn[&BB] = std::move(NewLiveIn);
          Changed = true;
        }
      }
    } while (Changed);
}
```

- Inicijalizacija: sve `{}`.
- Petlja ide **uvek istim redosledom** blokova, onim kojim su definisani u funkciji (`entry, BB1, BB2, ...`) — ovaj redosled se ne menja kroz runde.
- Za svaki blok: `NewLiveOut` = unija `LiveIn` svih direktnih sledbenika (korišćenjem **trenutnih**, možda još neažuriranih vrednosti).
- `NewLiveIn` se računa pozivom `computeLiveIn`.
- Ako se bilo šta promenilo → ažuriraj i postavi `Changed = true` → petlja se ponavlja.
- Kraj: kad jedna runda ne promeni ništa.

### 5.4 `eliminateDeadStoresInBasicBlock` — stvarno brisanje

```cpp
bool eliminateDeadStoresInBasicBlock(BasicBlock &BB) {
    bool Changed = false;
    std::vector<Instruction *> InstructionsToRemove;
    PtrSet Live = LiveOut[&BB];

    for (auto It = BB.rbegin(); It != BB.rend(); ++It) {
      Instruction &Instr = *It;
      if (StoreInst *SI = dyn_cast<StoreInst>(&Instr)) {
        Value *Ptr = SI->getPointerOperand();
        if (!Live.count(Ptr)) {
          InstructionsToRemove.push_back(SI);
          Changed = true;
          continue;            // NE radi erase — store se uklanja, kao da ga nikad nije bilo
        }
        Live.erase(Ptr);
      } else if (LoadInst *LI = dyn_cast<LoadInst>(&Instr)) {
        Live.insert(LI->getPointerOperand());
      }
    }

    for (Instruction *I : InstructionsToRemove) I->eraseFromParent();
    return Changed;
}
```

Ista unazad-petlja kao `computeLiveIn`, ali sa dodatnom proverom kod store-a:

- **`!Live.count(Ptr)`** — ako pointer **nije** live u ovom trenutku, znači da ništa dalje (ni u ostatku bloka, ni u sledbenicima preko `LiveOut`) ne čita vrednost koju ovaj store postavlja → **mrtav store** → ide u listu za brisanje, `continue` (bez erase-a, jer obrisana instrukcija ne treba da utiče na `Live`).
- Ako **jeste** live → store ostaje, `erase(Ptr)` gasi tu potrebu za sve što je pre njega.

Brisanje se radi **nakon** kompletne petlje (`eraseFromParent` u posebnoj petlji), da se ne pokvari iterator tokom iteracije unazad.

### 5.5 `runOnFunction` — povezuje sve

```cpp
bool runOnFunction(Function &F) override {
    bool Changed = false, LocalChanged;
    do {
      LocalChanged = false;
      computeLiveness(F);
      for (BasicBlock &BB : F)
        if (eliminateDeadStoresInBasicBlock(BB)) { LocalChanged = true; Changed = true; }
    } while (LocalChanged);
    return Changed;
}
```

1. Izračunaj liveness za **trenutni** IR.
2. Obriši mrtve store-ove u svim blokovima.
3. Pošto brisanje može **otkriti nove** mrtve store-ove (liveness se promeni kad se neki store ukloni) — ponovi **sve od početka** dok se ništa više ne menja.

---

## 6. Primer 1 — funkcija sa granjanjem

```llvm
define dso_local i32 @g(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  store i32 0, ptr %3, align 4
  %4 = load i32, ptr %2, align 4
  %5 = icmp sgt i32 %4, 0
  br i1 %5, label %6, label %13

6:                                                ; preds = %1
  store i32 5, ptr %3, align 4
  %7 = load i32, ptr %2, align 4
  %8 = icmp sgt i32 %7, 10
  br i1 %8, label %9, label %12

9:                                                ; preds = %6
  %10 = load i32, ptr %2, align 4
  %11 = add nsw i32 %10, 1
  store i32 %11, ptr %2, align 4
  br label %12

12:                                               ; preds = %9, %6
  store i32 7, ptr %3, align 4
  br label %13

13:                                               ; preds = %12, %1
  %14 = load i32, ptr %3, align 4
  ret i32 %14
}
```

CFG: `entry → {6,13}`, `6 → {9,12}`, `9 → {12}`, `12 → {13}`, `13 → {}`.

### Konačan rezultat fixpoint algoritma (do kojeg se stiže kroz 2-3 runde):

| Blok | LiveIn | LiveOut |
|---|---|---|
| entry (%1) | `{}` | `{%2, %3}` |
| 6 | `{%2}` | `{%2}` |
| 9 | `{%2}` | `{}` |
| 12 | `{}` | `{%3}` |
| 13 | `{%3}` | `{}` |

### Eliminacija u `entry` (LiveOut = `{%2,%3}`), unazad:

| Instr | Live pre provere | Odluka |
|---|---|---|
| `load %2` (%4) | `{%2,%3}` | — (load, insert no-op) |
| `store %3,0` | `{%2,%3}` | `%3` live → **ostaje**, erase(%3) → `{%2}` |
| `store %0,%2` | `{%2}` | `%2` live → **ostaje**, erase(%2) → `{}` |

Nijedan store u `entry` nije mrtav — oba se koriste dalje (`%2` se čita u `6`/`9`, `%3` se čita u `13`).

### Najvažnija lekcija ovog primera

Store `store i32 5, ptr %3` u bloku `6` (na ulazu u petlju/granu) **ostaje živ**, jer `LiveOut(12) = {%3}` — `%3` se čita na kraju u bloku `13`, bez obzira na to koji put se ide kroz `9`/`12`. Liveness preko CFG-a se prirodno "provlači" kroz fixpoint iteraciju čak i kroz grananja i (potencijalne) petlje, bez potrebe da algoritam posebno prati svaku putanju.

---

## 7. Primer 2 — direktna eliminacija u jednom bloku

```llvm
15:
  store i32 100, ptr %6, align 4   ; (1)
  store i32 200, ptr %6, align 4   ; (2)
  store i32 5, ptr %4, align 4     ; (3)
  %16 = load i32, ptr %6, align 4  ; (4)
  call void @f(i32 noundef %16)
  br label %19
```

Pretpostavka: `LiveOut(15) = {}` (ništa posle ovog bloka ne čita `%4` ni `%6`).

Unazad:

| Instr | Live pre provere | Odluka | Live posle |
|---|---|---|---|
| `br`/`call` | `{}` | — | `{}` |
| (4) `load %6` | `{}` | insert(%6) | `{%6}` |
| (3) `store %4,5` | `{%6}` | `%4` NIJE live → **MRTAV** → briše se, `continue` | `{%6}` (nepromenjeno) |
| (2) `store %6,200` | `{%6}` | `%6` JE live → **ostaje**, erase(%6) | `{}` |
| (1) `store %6,100` | `{}` | `%6` NIJE live → **MRTAV** → briše se, `continue` | `{}` |

**Obrisani:** (1) i (3). **Ostaje:** (2) i (4).

Rezultat nakon eliminacije:
```llvm
15:
  store i32 200, ptr %6, align 4
  %16 = load i32, ptr %6, align 4
  call void @f(i32 noundef %16)
  br label %19
```

Intuicija: (1) je odmah prepisan store-om (2) bez ikakvog čitanja između njih → mrtav. (3) piše u `%4`, koji se nigde ne čita (pretpostavka iz LiveOut) → mrtav. (2) ostaje, jer ga (4) čita.

---


## 8. Sažetak — ceo tok pass-a

```
runOnFunction
  └─ do:
       ├─ computeLiveness(F)                     // fixpoint preko CFG-a
       │    └─ za svaki BB: NewLiveOut = ⋃ LiveIn(Succ)
       │                    NewLiveIn  = computeLiveIn(BB, NewLiveOut)
       │                                   └─ unazad: load→insert, store→erase
       └─ za svaki BB: eliminateDeadStoresInBasicBlock(BB)
              └─ unazad: store mrtav ako pointer nije u Live → briše se
     while (nešto obrisano)
```

**Ključne ideje koje povezuju sve delove:**
1. Liveness se računa **unazad** unutar bloka, jer zavisi od "budućnosti" koda.
2. Liveness preko blokova zahteva **fixpoint iteraciju**, jer CFG može imati cikluse.
3. Mrtav store = store čiji pointer **nije live** u trenutku kad se na njega naiđe (idući unazad).
4. Brisanje store-ova može otkriti nove mrtve store-ove → **ponavlja se** ceo proces dok se ništa više ne menja.
5. Bez alias analize, ispravnost pass-a zavisi od toga da se pointer-i **ne aliasuju** — ograničenje koje treba imati na umu pri korišćenju.