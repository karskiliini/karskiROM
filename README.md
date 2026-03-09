# karskiROM — Custom C64 KERNAL ROM

Custom Commodore 64 KERNAL ROM -korvike, joka tuo nopean 2-bit-siirtoprotokollan
ja LZ4-pakkauksen purun. Yhteensopiva kaikkien IEC-laitteiden kanssa.

## Ominaisuudet

- **2-bit-siirtoprotokolla** — JiffyDOS-yhteensopiva nopea siirto (~5 KB/s)
- **LZ4-pakkauksen purku** — pakatun datan purku lennossa LOADin aikana (~9 KB/s efektiivinen)
- **Täysi taaksepäin-yhteensopivuus** — toimii kaikkien IEC-laitteiden kanssa
- **DOS wedge** — `@$` hakemistolistaus, F-näppäinoikotiet (jos tilaa riittää)

## Yhteensopivuus

karskiROM toimii kaikkien IEC-laitteiden kanssa. Nopea siirto ja pakkaus
aktivoituvat automaattisesti kun molemmat päät tukevat niitä.

| Asema | Siirtoprotokolla | Pakkaus | Nopeus |
|-------|------------------|---------|--------|
| Normaali 1541/1571/1581 | Standardi IEC | Ei | ~0.4 KB/s |
| JiffyDOS 1541 | 2-bit | Ei | ~5 KB/s |
| **64korppu** | **2-bit + LZ4** | **Kyllä** | **~9 KB/s** |
| SD2IEC | Standardi IEC | Ei | ~0.4 KB/s |
| Pi1541 | Standardi IEC | Ei | ~0.4 KB/s |

## Protokollakerrokset

```
Kerros 3:  Pakkaus      LZ4 lohkoprotokolla     ← XZ-komentokanavasopimus
Kerros 2:  Siirto       JiffyDOS 2-bit / std    ← autodetect ATN-ajoituksesta
Kerros 1:  Fyysinen     IEC CLK+DATA             ← aina sama rauta
```

Jokainen kerros on itsenäinen. 2-bit-siirto toimii minkä tahansa
JiffyDOS-yhteensopivan aseman kanssa. LZ4-pakkaus vaatii 64korppu-aseman
(tai muun XZ-protokollaa tukevan laitteen).

## 2-bit-siirtoprotokolla

Wire-tasolla identtinen JiffyDOS:n kanssa. Asema ei erota karskiROM:ia
JiffyDOS:sta.

### Tunnistus (kaksisuuntainen handshake)

1. C64 (karskiROM) vapauttaa ATN-linjan
2. C64 pitää DATA:n alhaalla ~260µs (200–320µs hyväksytty)
3. Asema mittaa pulssin ja tunnistaa JiffyDOS-handshaken
   - Tunnistettu → 2-bit mode
   - Ei tunnistettu → fallback standardi IEC

### Tavunsiirto

4 kierrosta per tavu, 2 bittiä per kierros CLK+DATA -linjoilla:

```
Kierros   CLK      DATA     Bitit
1         bit 0    bit 1    LSB-pari
2         bit 2    bit 3
3         bit 4    bit 5
4         bit 6    bit 7    MSB-pari

Ajoitukset:
  Kierros:        ~13µs
  Tavu yhteensä:  ~52µs
  EOI-viive:      ~200µs (ennen viimeistä tavua)
  Tavuväli:       ~30µs
```

## LZ4-pakkausprotokolla

### Aktivointi

karskiROM lähettää bootissa komentokanavallle (SA 15):

```basic
OPEN 15,8,15,"XZ:1" : CLOSE 15
```

Tämä käskee asemaa (64korppu) pakkaamaan kaiken TALK-datan LZ4:llä.
Asetus pysyy voimassa kunnes `XZ:0`, laite-reset tai IEC bus reset.

### Komennot

```
XZ:1    Aktivoi LZ4-pakkaus
XZ:0    Poista pakkaus käytöstä (oletus)
XZ:S    Kysy tila → vastaus "XZ:0" tai "XZ:1"
```

Standardi JiffyDOS ROM ja normaali KERNAL eivät lähetä XZ-komentoja,
joten pakkaus ei koskaan aktivoidu vahingossa.

### Lohkoprotokolla

Kun pakkaus on aktiivinen, tiedostodata ja hakemistolistaus lähetetään
lohkoissa normaalin tavuvirran sijaan:

```
Per lohko:
  ┌─────────────────────────────────────┐
  │ compressed_size  [2B, little-endian]│  Pakatun datan koko (1–512)
  │ raw_size         [2B, little-endian]│  Puretun datan koko (1–256)
  │ payload          [N tavua]          │  LZ4 block -pakattu data
  └─────────────────────────────────────┘

EOF:
  ┌─────────────────────────────────────┐
  │ 0x0000           [2B]              │  Ei enää lohkoja
  └─────────────────────────────────────┘
```

### C64-puolen purkulooppi (pseudokoodi)

```
load_compressed:
    jsr read_word           ; compressed_size → $FB/$FC
    lda $FB
    ora $FC
    beq .eof                ; 0 = valmis

    jsr read_word           ; raw_size → $FD/$FE
    jsr read_block          ; lue compressed_size tavua puskuriin
    jsr lz4_decompress      ; pura → kohdeosoitteeseen

    clc
    lda dest_ptr
    adc $FD                 ; dest_ptr += raw_size
    sta dest_ptr
    lda dest_ptr+1
    adc $FE
    sta dest_ptr+1

    jmp load_compressed

.eof:
    rts
```

### LZ4 block -formaatti (lohkon sisällä)

Standardi LZ4 block format (ei frame headeria):

```
Sequence:
  [token]         1 tavu: ylin 4b = literal count, alin 4b = match length - 4
  [literals*]     0-N literaalitavua
  [offset]        2B LE, match-etäisyys taaksepäin
  [match-ext*]    jos match length nybble == 15

Jos literal count == 15:
  lue lisätavuja, summaa kunnes tavu < 255

Viimeinen sequence: ei offset/match-kenttää (vain literaaleja)
```

## Arkkitehtuuri

### ROM-kartta (8 KB, $E000–$FFFF)

```
$E000-$E7FF   Muokattu KERNAL (I/O, näppäimistö, näyttö)
$E800-$EDFF   2-bit-siirtorutiinit (send/receive)
$EE00-$EFFF   LZ4-purkaja + lohkoprotokolla
$F000-$F4FF   LOAD/SAVE-rutiinit (muokattu)
$F500-$FDFF   Muu KERNAL (BASIC-tuki, IRQ, NMI)
$FE00-$FE9F   DOS wedge (@$, F-näppäimet)
$FEA0-$FFF9   Vektoritaulut, KERNAL jumptable
$FFFA-$FFFF   NMI/RESET/IRQ-vektorit
```

### Vapaan tilan arvio

```
Alkuperäinen KERNAL:     ~7000 B
2-bit-siirto:             ~500 B
LZ4-purku:                ~200 B
Lohkoprotokolla:          ~100 B
XZ-komentokanavatuki:     ~100 B
DOS wedge (suppea):       ~200 B
─────────────────────────────────
Yhteensä:                ~8100 B  → mahtuu juuri ja juuri 8 KB:hen
```

## Kehitysympäristö

### Työkalut

- **cc65** — 6502 C-kääntäjä ja assembler
- **VICE** — C64-emulaattori testaukseen
- **64tass** tai **ca65** — assembler (vaihtoehto)

### Testaus

1. **VICE-emulaattori:** karskiROM KERNAL-imagena, 64korppu simuloituna
2. **Oikea rauta:** EPROM-polttaja tai flash-cartridge C64:ssä
3. **Regressiotestit:** varmista standardi-IEC-yhteensopivuus kaikilla laitteilla

## Liittyvät projektit

- **[64korppu](https://github.com/marski/64korppu)** — C64 + PC floppy -yhdistäjä (Nano-firmware)
- LZ4-pakkaus Nano-puolella: `firmware/E-IEC-Nano-SRAM/src/lz4_compress.c`
- Protokolladokumentaatio: `docs/E-IEC-Nano-SRAM/lz4-protokolla.md`

## Lisenssi

TBD

## Tila

Suunnitteluvaihe. Protokolla dokumentoitu, toteutus ei alkanut.
