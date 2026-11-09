# Glucoid Studio

Paikallinen glukoosi- ja kirjaushistorian tarkastelu Linuxin KDE Plasma -työpöydälle.
Käyttöliittymä on suomeksi. Glucoid Studio on erillinen sovellus;
[Glucoid](https://github.com/reijokin-sys/Glucoid) on Plasma-paneelin widget.

## Toiminnot

- Yhteenveto: aikapainotettu glukoosikeskiarvo, GMI, tavoitealueaika,
  vaihtelu, mittauskattavuus, yöajan keskiarvo ja vuorokausiprofiili.
- Vuorokausianalyysi seitsemässä kellovälissä, myös 00–06: glukoosin
  keskiarvot sekä kirjauksiin perustuva insuliini/HH-suhde.
- Kuluvan ja valittujen päivien käyrät, paikallinen historia ja varmuuskopiot.
- Yksi aktiivinen lähde: Juggluco tai oma Nightscout-palvelin.

Jugglucon verkkoyhteys vaatii Linuxin toimivan WireGuard-liittymän ja
puhelimen numeerisen tunneliosoitteen. Studio sitoo verkkoyhteyden kyseiseen
liittymään; suojaamattomaan reittiin ei vaihdeta. Vaihtoehtona on USB/ADB.
Erillinen Nightscout käyttää HTTPS/API v3:a eikä vaadi WireGuardia.
Lukutunnukset säilytetään KDE-avainrenkaassa. Tuonti tapahtuu käynnistyessä
ja viiden minuutin välein sovelluksen ollessa auki.

Ohjelma kuvaa tallennettuja tietoja. Se ei takaa lähdetietojen oikeellisuutta.
GMI on sensoridatasta laskettu arvio, ei mitattu laboratorio-HbA1c.
Historialliset, kliinisesti validoimattomat tutkimustyökalut ovat erillisessä
`--research`-tilassa; niitä ei pidä käyttää itsenäisiin lääkityspäätöksiin.

## Asennus

Julkaisun .deb on tarkoitettu **Ubuntu/Kubuntu 26.04 amd64** -järjestelmään,
Qt 6.10:lle ja KDE/Kirigami 6:lle. Se käyttää jakelun kirjastoja eikä sisällä
niiden omia kopioita.

```sh
sudo apt install ./glucoid-studio_1.4.4_amd64.deb
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

## Lähdekoodista rakentaminen

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

## Laskentaperiaate

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

## Lisenssi

GPL-3.0-or-later. Katso [LICENSE](LICENSE) ja
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
