# Plugin API

## Rozhraní

```cpp
class IModePlugin {
public:
    virtual std::string_view id() const noexcept = 0;
    virtual Mode mode() const noexcept = 0;
    virtual int priority() const noexcept = 0;

    virtual Detection detect(const IQBlock& block) = 0;
    virtual void startSession(const SessionContext& ctx) = 0;
    virtual bool process(const IQBlock& block) = 0;
    virtual void stopSession() = 0;
};
```

## Životní cyklus

1. **Monitoring** — dispatcher volá `detect()` na všech pluginů pro každý IQ
   blok. Plugin vrátí `Detection{detected, confidence}`.
2. **Zisk vlastnictví** — SessionManager vybere vítěze a zavolá
   `startSession(ctx)`. `SessionContext` obsahuje `sessionId`, `mode`,
   `pluginId` a `ITransmit*` handle pro vysílání.
3. **Exkluzivní zpracování** — jen vítěz dostává `process(block)`. Vrací
   `false`, když relace skončila (EOT / ztráta nosné).
4. **Uvolnění** — `stopSession()` se volá při normálním konci, timeoutu nebo
   shutdownu.

## Priority

- M17: 30
- D-Star: 20
- FM: 10

Vyšší hodnota vítězí; shoda se řeší stabilním pořadím registrace.

## Detekce

- FM — NFM diskriminátor + Goertzel na CTCSS tónu. FM detekuje jen při platném
  CTCSS.
- D-Star — GMSK demod + korelace frame sync `0x55 0x55 0x6E 0x0A`.
- M17 — 4FSK demod + korelace sync slov: LSF `0x55F7`, Stream `0xFF5D`,
  Packet `0x75FF`, EOT `0x555D`.

## Příklad: nový plugin

```cpp
class MyPlugin final : public sx1255::IModePlugin {
public:
    std::string_view id() const noexcept override { return "my"; }
    sx1255::Mode mode() const noexcept override { return sx1255::Mode::Unknown; }
    int priority() const noexcept override { return 5; }

    sx1255::Detection detect(const sx1255::IQBlock& block) override { /* ... */ }
    void startSession(const sx1255::SessionContext& ctx) override { /* ... */ }
    bool process(const sx1255::IQBlock& block) override { return true; }
    void stopSession() override { /* ... */ }
};
```

Registrace:

```cpp
registry.registerPlugin(std::make_shared<MyPlugin>());
```
