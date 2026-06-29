---
theme: ./light.json
author: Vasilije Paunović
date: Jun 2026
paging: Slajd %d / %d
---

# Eliminacija mrtvih argumenata

### LLVM `ModulePass`

Prolaz koji pronalazi funkcije sa argumentima koji se ne koriste i menja potpis funkcije,
kao i pozive, tako da navode samo argumente koji se zapravo koriste u telu funkcije.

---

## Problem

**Mrtav argument** je parametar koji se nikada ne koristi unutar tela funkcije.

```c
// 'b' se prosleđuje, ali se nikada ne koristi.
int add(int a, int b) {
    return a;
}

int main() {
    int x = add(1, 2);   // 2 se bespotrebno prosledjuje
    return 0;
}

```

Svako mesto poziva i dalje računa i prosleđuje `2`.
Rezultat: protraćeni registri, mesta na steku i instrukcije.

**Cilj:** prepraviti `add` da prima samo `a` i popraviti svakog pozivaoca.

---

## Realan primer: Ciscenje posle Interprocedural Constant Propagation

```cpp
int applyTax(int amount, int rate) {
    return amount * rate / 100;
}

applyTax(a, 20);   // svi pozivi prosledjuju isti rate
applyTax(b, 20);   // (npr. iz konfiguracije)
```

Constant Propagation vidi da je `rate` svuda ista konstanta i zameni ga u telu.
`rate` više niko ne čita:

```cpp
int applyTax(int amount, int rate) {   // rate je sad mrtav
    return amount * 20 / 100;
}
```

**DAE** skloni mrtav parametar iz potpisa i svih poziva:

```cpp
int applyTax(int amount) { return amount * 20 / 100; }
applyTax(a);
applyTax(b);
```

---

## Dva nivoa strukture

Prolaz je izgrađen od dve funkcije:

- `runOneIteration(...)` — radi jedan ceo prolaz kroz modul
- `run(...)` — poziva `runOneIteration` dok god ima promena*

```cpp
PreservedAnalyses run(Module &Mod, ModuleAnalysisManager &mam) {
    bool IRModified = false;
    while (runOneIteration(Mod, mam)) {
        if(modified)
            IRModified = true;
    }
    return IRModified ? PreservedAnalyses::none()
                      : PreservedAnalyses::all();
}
```

Proći ćemo kroz jednu iteraciju kao niz koraka,
a zatim se vratiti na to *zašto* ponavljamo.

---

## Korak 1 — Označi kandidate

Prođi kroz svaku funkciju. Ako **bilo koji** argument nema upotreba,
funkcija je kandidat za prepravku.

```cpp
std::vector<Function *> deadArgFuncs;
for (auto &Func : Mod.getFunctionList()) {
    bool dead = false;
    for (auto &Arg : Func.args()) {
        if (Arg.use_empty())
            dead = true;
    }
    if (dead)
        deadArgFuncs.push_back(&Func);
}
```

`Arg.use_empty()` vraca informaciju "Da li ista koristi ovaj argument na bilo koji nacin?"

Alternativa: Za svaki argument, prodjemo kroz svaku instrukciju svakog basic blocka trenutne funkcije i vidimo da li se argument koristi

---

## Korak 2 — Pronađi mrtve indekse

Za svaku označenu funkciju, zapamti **koje** pozicije argumenata su mrtve.

```cpp
std::vector<size_t> deadArgs;
for (size_t argIndex = 0; argIndex < Func->arg_size(); argIndex++) {
    auto Arg = Func->getArg(argIndex);
    if (Arg->use_empty()) {
        deadArgs.push_back(argIndex);
    }
}
```

Radimo sa **indeksima**, a ne sa samim objektima argumenata.

Indeksi su ono što povezuje potpis funkcije sa svakim mestom poziva —
argument *N* funkcije se poklapa sa operandom *N* poziva.

---

## Korak 3 — Izračunaj preživele argumente

Nova lista parametara je `originalniParametri − mrtviParametri`.

Čuvamo mapu `indeks → tip` da bismo zapamtili **originalnu poziciju**
svakog preživelog argumenta.

```cpp
std::map<size_t, Type *> survivingParams;
for (size_t i = 0; i < Func->getFunctionType()->params().size(); i++) {
    if (std::ranges::find(deadArgs, i) == deadArgs.end()) {
        survivingParams[i] = Func->getArg(i)->getType();
    }
}
```

Taj originalni indeks nam vrsi dosta posla. Trebace nam jos dva puta:
jednom za prepravku tela, jednom za filtriranje operanada poziva.

---

## Korak 4 — Napravi novi tip funkcije

Izvuci tipove preživelih iz mape i zatraži od LLVM-ove
`FunctionType` fabrike odgovarajući tip.

```cpp
auto values = std::views::values(survivingParams);
std::vector<Type *> paramTypes(values.begin(), values.end());

auto newFuncType = FunctionType::get(
    Func->getReturnType(),   // isti povratni tip
    paramTypes,              // skraceni parametri
    Func->isVarArg());       // isti variadic flag
```

Menja se samo lista parametara. Povratni tip i
`varargs` zastavica se prenose nepromenjeni.

---

## Korak 5 — Napravi novu funkciju i premesti telo

Ne možemo menjati potpis funkcije u mestu, pa pravimo **novu**
funkciju i fizički premeštamo staro telo u nju.

```cpp
auto newFunc = Function::Create(newFuncType, Func->getLinkage(), Func->getAddressSpace(), "", &Mod);

newFunc->takeName(Func);              // preuzmi staro ime
newFunc->splice(newFunc->begin(), Func);  // PREMESTI sve bazne blokove
```

`splice` **premešta** bazne blokove — bez kopiranja, bez
ponovnog mapiranja lokalnih instrukcija. Telo je netaknuto...

...ali i dalje pokazuje na **stare** objekte argumenata:

```llvm
; PRE splice
define i32 @foo(i32 %a, i32 %dead) {
  %r = add i32 %a, 1
  ret i32 %r
}
```

```llvm
; POSLE splice: nova funkcija ima SVOJ %a.new, ali premešteno telo i dalje koristi stari %a
define i32 @foo(i32 %a.new) {
  %r = add i32 %a, 1     ; <- %a nije definisan ovde => nevalidan IR!
  ret i32 %r
}
```

`splice` je premestio instrukcije, ali ne i veze ka argumentima.
Zato Korak 6 (RAUW) zameni `%a` → `%a.new`.

---

## Korak 6 — Prepravi preživele argumente

Prevezi svaki preziveli argument na odgovarajući novi argument.

```cpp
int newArgIndex = 0;
for (auto key : std::views::keys(survivingParams)) {
    Func->getArg(key)
        ->replaceAllUsesWith(newFunc->getArg(newArgIndex++));
}
```

Zato smo i čuvali originalne indekse: `key` redom prolazi kroz stare
pozicije, dok `newArgIndex` prolazi kroz nove, zbijene.

Primer: `foo(a, b, c, d)` gde je `b` (indeks 1) mrtav:

```text
stari indeks (key):   0    1(mrtav)   2    3
novi indeks:          0      —        1    2

key=0  ->  newArgIndex=0     %a  ->  %a.new
key=2  ->  newArgIndex=1     %c  ->  %c.new
key=3  ->  newArgIndex=2     %d  ->  %d.new
```

Stari i novi indeks se razilaze cim preskocimo mrtav argument,
zato nam treba dva brojaca, ne jedan.

---

## Korak 7 — Prepravi pozive

Funkcija je popravljena, ali svaki pozivalac i dalje zove staru
sa svim argumentima. Prođi kroz korisnike.

```cpp
for (auto Use : Func->users()) {
    if (auto CB = dyn_cast<CallBase>(Use)) {
        std::vector<Value *> survivingArgs;
        for (size_t i = 0; i < CB->arg_size(); i++) {
            if (std::ranges::find(deadArgs, i) == deadArgs.end())
                survivingArgs.push_back(CB->getArgOperand(i));
        }
        auto newCall = CallInst::Create(newFuncType, newFunc, survivingArgs, "", CB->getIterator());
        CB->replaceAllUsesWith(newCall);
        newCall->takeName(CB);
        callsToErase.push_back(CB);   // odloži brisanje
    }
}
```

---

## Korak 8 — Odloži brisanje

Primetite da ništa nismo brisali unutar petlji.
Stari pozivi i stare funkcije idu na liste za posao umesto toga.

```cpp
funcsToErase.push_back(Func);
// ... kasnije, nakon što su sve funkcije obrađene:
for (auto CB : callsToErase) {
    CB->eraseFromParent();
    IRModified = true;
}
for (auto Func : funcsToErase) {
    Func->eraseFromParent();
    IRModified = true;
}
```

Iteriramo kroz funkcije i kroz `users()`.
Brisanje tokom iteracije bi poništilo same opsege kroz koje prolazimo.

---

## Zašto petlja? — Lančani mrtvi argumenti

Jedan prolaz može da *stvori* nove mrtve argumente. Razmotrite:

```c
// Pre iteracije
int helper(int x) { return 0; }     // x je mrtav
int caller(int y) {
    return helper(y);
}

// Nakon prve iteracije
int helper() { return 0; }
int caller(int y) {                 // y je sad mrtav -> moze da se izbaci
    return helper();
}

// Nakon druge iteracije
int helper() { return 0; }
int caller() {
    return helper();
}
```

- Iteracija 1: `helper`-ov `x` je mrtav → izbaci ga.
  Sada poziv `helper(y)` više ne koristi `y`...
- Iteracija 2: `caller`-ov `y` je sada mrtav → izbaci ga.

`run()` ponavlja `runOneIteration` dok jedan prolaz ništa ne promeni

---

## Sažetak

Prolaz, u jednom dahu:

1. **Označi** funkcije sa bilo kojim nekorišćenim argumentom
2. **Pronađi** indekse mrtvih argumenata
3. **Sačuvaj** preživele (indeks → tip)
4. **Napravi** skraćeni `FunctionType`
5. **Stvori** novu funkciju, **premesti** telo u nju
6. **Prepravi** stare argumente → nove argumente
7. **Prevezi** svako mesto poziva, izbacujući mrtve operande
8. **Obriši** stare pozive i funkcije (odloženo)

...i sve to **`run()` ponavlja do fiksne tačke** (dok jedan prolaz ništa ne promeni).

---

# Hvala

### Pitanja?

`DeadArgumentEliminationPass.cpp`

Pokretanje:  `opt -load-pass-plugin ./libDAE.so -passes=dae input.ll`
