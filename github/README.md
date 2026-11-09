# Glucoid Studio

![Glucoid Studio](packaging/fi.reijo.glucoidstudio.png)

[Suomeksi](#suomeksi-english-below) · [English](#english)

### Suomeksi (English below)

Paikallinen glukoosi- ja kirjaushistorian tarkastelu Linuxin KDE Plasma -työpöydälle.
Käyttöliittymä on suomeksi. Glucoid Studio on erillinen sovellus;
[Glucoid](https://github.com/reijokin-sys/Glucoid) on Plasma-paneelin widget.

#### Toiminnot

- Oma lila–mintunvihreä G-kuvake, joka perustuu tekijän käsiluonnokseen.
- Yhteenveto: aikapainotettu glukoosikeskiarvo, GMI, tavoitealueaika,
  vaihtelu, mittauskattavuus, yöajan keskiarvo ja vuorokausiprofiili.
- Vuorokausianalyysi seitsemässä kellovälissä, myös 00–06: glukoosin
  keskiarvot sekä kirjauksiin perustuva insuliini/HH-suhde.
- Kuluvan päivän käyrä, vuorokausiprofiili ja vieritettävä neljän päivän
  vertailu. Tavoitealueen ulkopuoliset käyräosuudet näkyvät punaisina.
- Paikallinen historia ja varmuuskopiot. HH-/insuliinisummat ovat
  vuorokausianalyysin avattavissa laskentatiedoissa.
- Yksi aktiivinen lähde: Juggluco tai oma Nightscout-palvelin.
- Versionumero näkyy sovelluksen nimen vieressä.

Jugglucon verkkoyhteys vaatii Linuxin toimivan WireGuard-liittymän ja
puhelimen numeerisen tunneliosoitteen. Studio sitoo verkkoyhteyden kyseiseen
liittymään; suojaamattomaan reittiin ei vaihdeta. Vaihtoehtona on USB/ADB.
Erillinen Nightscout käyttää HTTPS/API v3:a eikä vaadi WireGuardia.
Lukutunnukset säilytetään KDE-avainrenkaassa. Tuonti tapahtuu käynnistyessä
ja viiden minuutin välein sovelluksen ollessa auki.

Ohjelma kuvaa tallennettuja tietoja. Se ei takaa lähdetietojen oikeellisuutta.
GMI on sensoridatasta laskettu arvio, ei mitattu laboratorio-HbA1c.
GMI:n viitearvoväri näytetään myös viikon jaksolle; lyhyt tai vajaa jakso merkitään suuntaa-antavaksi.
Historialliset, kliinisesti validoimattomat tutkimustyökalut ovat erillisessä
`--research`-tilassa; niitä ei pidä käyttää itsenäisiin lääkityspäätöksiin.

#### Asennus

Julkaisun .deb on tarkoitettu **Ubuntu/Kubuntu 26.04 amd64** -järjestelmään,
Qt 6.10:lle ja KDE/Kirigami 6:lle. Se käyttää jakelun kirjastoja eikä sisällä
niiden omia kopioita.

```sh
sudo apt install ./glucoid-studio_1.5.4_amd64.deb
glucoid-studio
```

Sovellus löytyy myös työpöydän sovellusvalikosta. Asetuksista valitaan
lähde ja yhteystapa. Sovellus ei asenna tai muuta WireGuard-tunnelia.
USB-tuonti edellyttää erikseen asennettua `adb`:tä ja laitteen USB-vianetsinnän
hyväksyntää.

Tiedot tallennetaan käyttäjän XDG-sovellustietohakemiston alle
(oletus `~/.local/share/glucoid-studio/live/`). Esimerkkitila `--demo` käyttää
erillistä demo-hakemistoa. Hakemiston voi valita itse `--data-dir`-valinnalla.
Salaisuuksia ei tallenneta yhteysasetustiedostoon. Raportit ja varmuuskopiot
ovat paikallisia; samalla levyllä oleva varmuuskopio ei suojaa levyvioilta.

```sh
sudo apt remove glucoid-studio
```

Poisto säilyttää käyttäjän historian ja avainrengastiedot.

#### Lähdekoodista rakentaminen

Tarvitaan C++20-kääntäjä, CMake, Ninja, SQLite3, Qt 6.10
(Core, Gui, Widgets, Quick, QuickControls2, Network, DBus, Test, Graphs,
Concurrent), KF6Wallet, Kirigami 6:n QML-moduulit, Python 3 ja testejä varten
OpenSSL. Tarkat ajonaikaiset riippuvuudet ovat .deb-paketin metatiedoissa.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 4
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
sudo cmake --install build --prefix /usr/local
```

Suurin osa testeistä käyttää väliaikaisia synteettisiä aineistoja.
Paikallisten HTTP/TLS-testien on saatava avata localhost-soketteja.
Turvallisuuslaboratorion nimiavaruusajurit eivät kuulu normaaliin
käyttöön eivätkä ne muuta käyttäjän puhelinta tai tunnelia.

Kehittäjän paikallisen aineiston sijainnin voi määritellä CMake-valinnalla
`STUDIO_LOCAL_DATA_ROOT`; julkinen paketti käyttää aina XDG-oletusta.

#### Laskentaperiaate

Glukoosi painotetaan mitatulla ajalla. Stream on ensisijainen ja
sensorihistoria täydentää aukkoja ilman päällekkäistä laskentaa. Puuttuvaa
aikaa ei käsitellä nollana eikä pitkää aukkoa täytetä viimeisellä arvolla.
Tekninen enimmäisraja on 20 minuuttia; todellinen katettu aika rajoittuu myös
sarjan havaittuun mittausväliin.

Vuorokausianalyysissä lasketaan ensin jokaisen päivän kellovälin keskiarvo,
sitten päiväkeskiarvojen keskiarvo. Jokainen mitattu päivä painaa yhtä paljon.
Etusivun keskiarvot painottavat koko valitun jakson katettua mittausaikaa.
Matalat ja korkeat arvot sekä ateria-/insuliinikirjaukset eivät sulje
kuvailevan glukoosianalyysin öitä tai päiviä pois. Klo 00–06 tarkoittaa
kelloväliä, ei varmennettua uniaikaa.

Insuliini/HH-suhde kuvaa kirjattuja määriä, ei annossuositusta. Se on
päivittäisten suhteiden keskiarvo niiltä päiviltä, joilla on positiivinen
HH-määrä ja pikainsuliinikirjaus. Puuttuva kirjaus ei ole nolla.

#### Lisenssi

GPL-3.0-or-later. Katso [LICENSE](LICENSE) ja
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

---

### English

Glucoid Studio is a local glucose and treatment-entry history viewer for the Linux KDE Plasma desktop. The application interface is in Finnish. It is a separate application from [Glucoid](https://github.com/reijokin-sys/Glucoid), the Plasma panel widget.

#### Features

- A purple and mint-green G icon based on the author’s hand-drawn sketch.
- Summary: time-weighted mean glucose, GMI, time in range, variability, data coverage, a nighttime mean and a daily profile.
- Daily analysis across seven time periods, including 00–06: glucose means and recorded insulin-to-carbohydrate ratios.
- A current-day chart, a daily profile and a scrollable four-day comparison. Curve portions outside the configured target range are red.
- Local history and backups. Carbohydrate and rapid-insulin totals are available in the expandable calculation details.
- One active data source: Juggluco or your own Nightscout server.
- The version number is shown next to the application name.

Juggluco network access requires a working Linux WireGuard interface and the phone's numeric tunnel address. Studio binds the connection to that interface and does not fall back to an unprotected route. USB/ADB is an alternative. Independent Nightscout access uses HTTPS/API v3 and does not require WireGuard. Read credentials are stored in KDE Wallet. Import runs on startup and every five minutes while the application is open.

The application describes recorded data; it cannot guarantee that source data is correct. GMI is an estimate derived from sensor readings, not a laboratory HbA1c measurement. Its reference color is also shown for a one-week period; short or incomplete periods remain marked as indicative. Historical, clinically unvalidated research tools remain available separately with `--research`; they must not be used for independent medication decisions.

#### Installation

The release `.deb` targets **Ubuntu/Kubuntu 26.04 amd64**, Qt 6.10 and KDE/Kirigami 6. It uses distribution-provided libraries rather than bundled copies.

```sh
sudo apt install ./glucoid-studio_1.5.4_amd64.deb
glucoid-studio
```

The application also appears in the desktop application menu. Choose the source and connection type in Settings. Studio does not install or configure a WireGuard tunnel. USB import requires `adb` to be installed separately and USB debugging to be approved on the device.

Data is stored below the user's XDG application-data directory (normally `~/.local/share/glucoid-studio/live/`). Demo mode, `--demo`, uses a separate demo directory. Override the data directory with `--data-dir`. Secrets are not stored in the connection settings file. Reports and backups are local; a backup on the same disk does not protect against disk failure.

```sh
sudo apt remove glucoid-studio
```

Removal preserves the user's history and wallet credentials.

#### Building from source

Requirements: a C++20 compiler, CMake, Ninja, SQLite3, Qt 6.10 (Core, Gui, Widgets, Quick, QuickControls2, Network, DBus, Test, Graphs and Concurrent), KF6Wallet, the Kirigami 6 QML modules, Python 3 and OpenSSL for the tests. Exact runtime dependencies are recorded in the `.deb` metadata.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 4
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
sudo cmake --install build --prefix /usr/local
```

Most tests use temporary synthetic datasets. Local HTTP/TLS tests need permission to open localhost sockets. The security laboratory's network-namespace drivers are separate from normal use; they do not modify a user's phone or tunnel.

Developers can set a local data root with the `STUDIO_LOCAL_DATA_ROOT` CMake option. The public package uses the XDG default.

#### Calculation policy

Glucose is weighted by measured duration. Stream data takes priority; sensor history fills only gaps, without double counting overlapping time. Missing time is not treated as zero, and the last reading is not extended across a long gap. The technical maximum gap is 20 minutes; covered time is also limited by the series' observed sampling interval.

Daily analysis first calculates a time-weighted mean for each day's time period, then averages those daily means with equal weight for each measured day. Front-page means weight all covered time in the selected reporting period. Low or high readings and carbohydrate/insulin entries do not exclude nights or days from descriptive glucose analysis. The 00–06 interval is a clock-time window, not verified sleep time.

The insulin-to-carbohydrate ratio describes recorded amounts, not a dose recommendation. It averages daily ratios for days with a positive carbohydrate amount and a rapid-insulin entry. A missing entry is not zero.

#### License

GPL-3.0-or-later. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
