#include "LoopStore.h"

#include <LittleFS.h>

namespace
{
// Anzeigenamen (ohne ".loop") der gefundenen Loops
char names[LOOP_STORE_MAX][24];
uint8_t nameCount = 0;

// Gemeinsamer Puffer (Kopf + Events; siehe LooperController-Format)
uint8_t buf[16 + LOOP_MAX_EVENTS * 9];

bool mounted = false;

// Verzeichnis scannen und die *.loop-Namen einsammeln
void scan()
{
    nameCount = 0;

    if (!mounted)
    {
        return;
    }

    File root = LittleFS.open("/");

    if (!root)
    {
        return;
    }

    for (File f = root.openNextFile(); f && nameCount < LOOP_STORE_MAX; f = root.openNextFile())
    {
        if (f.isDirectory())
        {
            continue;
        }

        String n = f.name();

        int slash = n.lastIndexOf('/'); // je nach Core-Version mit/ohne Pfad

        if (slash >= 0)
        {
            n = n.substring(slash + 1);
        }

        if (!n.endsWith(".loop"))
        {
            continue;
        }

        n = n.substring(0, n.length() - 5); // ".loop" abschneiden

        n.toCharArray(names[nameCount], sizeof(names[0]));

        nameCount++;
    }
}
} // namespace

void LoopStore::begin()
{
    mounted = LittleFS.begin(true); // true = bei Mount-Fehler formatieren

    if (mounted)
    {
        scan();
    }

    Serial.print("LoopStore: ");
    Serial.print(mounted ? "LittleFS bereit, " : "kein Dateisystem, ");
    Serial.print(nameCount);
    Serial.println(" Loop(s)");
}

uint8_t LoopStore::count()
{
    return nameCount;
}

const char* LoopStore::name(uint8_t index)
{
    return index < nameCount ? names[index] : "";
}

uint8_t* LoopStore::buffer()
{
    return buf;
}

size_t LoopStore::bufferSize()
{
    return sizeof(buf);
}

size_t LoopStore::read(uint8_t index)
{
    if (!mounted || index >= nameCount)
    {
        return 0;
    }

    String path = String("/") + names[index] + ".loop";

    File f = LittleFS.open(path, "r");

    if (!f)
    {
        return 0;
    }

    size_t n = f.read(buf, sizeof(buf));

    f.close();

    return n;
}

const char* LoopStore::saveNext(size_t len)
{
    if (!mounted || len == 0 || len > sizeof(buf))
    {
        return nullptr;
    }

    // Nächsten freien Namen loop1..loop99 finden
    static char saved[24];

    for (int i = 1; i <= 99; i++)
    {
        snprintf(saved, sizeof(saved), "loop%d", i);

        String path = String("/") + saved + ".loop";

        if (LittleFS.exists(path))
        {
            continue;
        }

        File f = LittleFS.open(path, "w");

        if (!f)
        {
            return nullptr;
        }

        size_t w = f.write(buf, len);

        f.close();

        if (w != len)
        {
            LittleFS.remove(path);

            return nullptr;
        }

        scan();

        return saved;
    }

    return nullptr; // voll
}
