/*
  Sistema monitoraggio aspirapellet.

  Il sistema è composto da una scheda arduino nano e da una scheda raspberry py (collegata ad internet).
  Lo scopo del sistema è quello di:
    - Monitorare il tempo di funzionamento del motore dell'aspirapellet (a causa di un guasto del PLC) ed eventualmente togliere l'alimentazione per evitare guasti al motore stesso.
    - Monitorare la spia di allarme dell'aspirapellet ed esportare l'allarme stesso.
    - Monitorare il livello di pellet nel sisol ed esportare il dato.

  NOTA: si utilizza il LED integrato nella scheda come "life bit"

  ATTENZIONE: tutti gli istanti di tempo in questo sketch sono espressi in millisecondi!

  TODO LIST:
    - Verificare funzionamento nuova funzione di controllo stato
    - inserire debouce senza delay - forse: si tratta di una finezza non necessaria che, però, potrebbe consumare memoria!
    - Inserire data e ora con il real time clock - Troppo dispendioso in termini di memoria: si salverà l'informazione lato raspberry in funzione della data/ora di sistema.
    - Gestire sensore ping
    - Gestire lettura spia allarme - da verificare
    - Verificare che funzioni la lettura corrente con il cast a double.
    - Verificare funzionamento spegnimento di sicurezza dopo aver commentato le righe inutili(?)
    - Verificare funzionamento nuovo array di acquisizione
    - Ricontrollare e pulire il codice dalle variabili inutili.

*/


//--------------------VARIABILI GLOBALI
// Dichiaro i numeri dei pin di interfacciamento
const int pinLifeBit = LED_BUILTIN;  // Pin di default: è il pin 13
const int pinRelayOn = 2;
const int pinRelayOff = 3;
const int pinRelayOnManuale = 12;
const int pinRelayOffManuale = 11;
const int pinLetturaCorrente = A0;
const int pinLetturaTensione = A1;
const int pinPingTrigger = 7;
const int pinPingEcho = 6;

// inizializzo i pin
int statoPinLifeBit = LOW;
int statoPinRelayOn = LOW;
int statoPinRelayOff = LOW;

// life bit
unsigned long istanteUltimaCommutazioneLifeBit = 0;
const long intervalloLifeBit = 1000;

// Record
const int periodoAcquisizione = 1;  // Corrisponde a 1000 Hz!
const int numeroAcquisizioni = 50;



// controllo periodo accensione del motore aspirapellet
String ultimoStatoNotoMotore = "OFF";
const float valorZeroPinCorrenteMotore = 2.7;    // la tensione a corrente nulla NON corrisponde a 2.5V, ma a 2.7V.
const float sogliaCorrenteMotoreOn = 0.5;  // Valore oltre il quale assumo stia passando corrente nel motore e che, quindi, sia acceso.
const int periodoMinimoMotoreOff = 100;  // Periodo minimo in cui la lettura di corrente che passa al motore deve risultare sotto la soglia affichè possa assumere che si sia spento: due periodi alla frequenza di rete (50Hz)!
unsigned long istanteAccensioneMotore = 0;
unsigned long durataAccensioneMotore = 0;
unsigned long primoIstanteSottoSogliaMotore = 0;
const unsigned long periodoMassimoAccensione = 30000;
bool inviareSpegnimentoDiSicurezza = false;

// controllo accensione spia di allarme
String ultimoStatoNotoAllarme = "OFF";
const int sogliaTensioneAllarmeOn = 768;
const int periodoMinimoAllarmeOff = 2000; // L'allarme ha una spia accesa/spenta con un periodo di 1s. Quindi imposto il periodo minimo di 2 secondi.
unsigned long istanteAccensioneAllarme = 0;
unsigned long durataAccensioneAllarme = 0;
unsigned long primoIstanteSottoSogliaAllarme = 0;






//--------------------FUNZIONI SUPPORTO
void comandaRelay(int numeroPin) {
  // Questa procedura comanda il relay attivando il pin relativo.
  // ATTENZIONE: il relay ha bisogno di un impulso di almeno 50ms, qundi si deve riportare il pin a zero.

  digitalWrite(numeroPin, HIGH);
  delay(60);
  digitalWrite(numeroPin, LOW);
}


unsigned long analizzaStatoGrandezzaSinusoidale(String& ultimoStatoNoto, double valoreGrandezza, double sogliaOn, unsigned long& istanteAccensione, int periodoMinimoOff, unsigned long& primoIstanteSottoSoglia) {
  // Questa funzione controlla lo stato di accensione di un sistema a grandezza sinudiodale, come la corrente a un motore o la tensione a una luce spia.
  // Ne aggiorna quindi i valori tramite le variabili rivcevute in input restituisce eventualmente la durata dell'ultima accensione.

  unsigned long durataAccensione = 0;
  
  if (ultimoStatoNoto == "OFF") {
    // Ultimo stato noto: SPENTO!

    // VERIFICA ACCENSIONE
    // Devo controllare se la grandezza fisica è sopra soglia!
    if (abs(valoreGrandezza) >= sogliaOn) {
      // Il sistema si è acceso!
      ultimoStatoNoto = "ON";
      istanteAccensione = millis();
    }
  } else if (ultimoStatoNoto == "ON") {
    // Ultimo stato noto: ACCESO!

    // VERIFICA SPEGNIMENTO
    // Devo quindi verificare che la grandezza fisica sia stabile sotto la
    // soglia almeno per la durata del periodo minimo!
    if (abs(valoreGrandezza) < sogliaOn) {
      // Il sistema POTREBBE essere spento
      // Controllo ancora di aver trovato il primo istante sotto soglia, per poter verificare il periodo minimo di spegnimento.
      if (primoIstanteSottoSoglia == 0) {
        // Ho trovato il primo istante!
        primoIstanteSottoSoglia = millis();
      } else {
        // Avevo già trovato il primo istante, quindi devo controllare che sia
        // costante da un po' di tempo!
        if ((millis() - primoIstanteSottoSoglia) >= periodoMinimoOff) {
          // Il sistema è effettivamente spento!
          durataAccensione = (millis() - istanteAccensione);
          ultimoStatoNoto = "OFF";
          primoIstanteSottoSoglia = 0;
        }
      }
    } else {
      // La grandezza è sopra la soglia: il sistema è ancora acceso!
      // Devo pulire le variabili!
      primoIstanteSottoSoglia = 0;
    }
  }

  return durataAccensione;

}


void RegistraGrandezze(const int& pinCorrenteMotore, const int& pinTensioneAllarme){

  // Questa procedura registra e stampa le letture dei due sesori di corrente al motore e di tensione dell'allarme.
  // NOTA IMPORTANTE: questa procedura restituisce le letture così come sono! Stampa quindi sulla seriale degli interi!

  unsigned long istanti[numeroAcquisizioni];
  int correnti[numeroAcquisizioni];
  int tensioni[numeroAcquisizioni];

  Serial.println("Inizio acquisizione");
  delay(10);
  for (byte i = 0; i <numeroAcquisizioni; i++){

    istanti[i] = millis();
    correnti[i] = analogRead(pinCorrenteMotore);
    tensioni[i] = analogRead(pinTensioneAllarme);
    
    delay (periodoAcquisizione);

  }

  Serial.println("Fine acquisizione e stampa risultati");
  delay(10);

  Serial.println("Istante; corrente; tensione");
  delay(10);

  for (byte j = 0; j <numeroAcquisizioni; j++){
    
    Serial.print(String(istanti[j]) + ";" + String(correnti[j]) + ";" + String(tensioni[j]) + "\n");
    
    delay (10);

  }    


}


int MisuraLivelloPellet(){

  // Quersta funzione misura la percentuale di livello del pellet


}

float MisuraAltezzaPellet_cm(){

  // Questa funzione misura l'altezza del pellet dal sensore in cm.

  //----- Eseguo il trigger
  // Pulisco il pin del trigger
  digitalWrite(pinPingTrigger, LOW);
  delay(1);
  // Eseguo il trigger vero e proprio: imposto ad ON per 10 micro secondi.
  digitalWrite(pinPingTrigger, HIGH);
  delayMicroseconds(10);
  digitalWrite(pinPingTrigger, LOW);

  //----- Leggo l'output
  unsigned long durata_us = pulseIn(pinPingEcho, HIGH);
  
  // Calculating the distance
  float distanza_cm = 0;
  distanza_cm = (float)durata_us * 0.034 / 2; // Speed of sound wave divided by 2 (go and back)
  
  return distanza_cm;
  
}




//--------------------SETUP
void setup() {
  // Imposto i vari pin e apro la comunicazione seriale.
  pinMode(pinLifeBit, OUTPUT);
  pinMode(pinRelayOn, OUTPUT);
  pinMode(pinRelayOff, OUTPUT);

  pinMode(pinLetturaCorrente, INPUT);
  pinMode(pinLetturaTensione, INPUT);

  pinMode(pinRelayOnManuale, INPUT);
  pinMode(pinRelayOffManuale, INPUT);

  pinMode(pinPingTrigger, OUTPUT);
  pinMode(pinPingEcho, INPUT);

  Serial.begin(9600);

}

//--------------------LOOP
void loop() {

  //----------DICHIARAZIONE VARIABILI
  String comandoManuale = "";

  //----------GESTIONE LIFE BIT
  if (millis() - istanteUltimaCommutazioneLifeBit >= intervalloLifeBit) {
    // aggiorno l'istante dell'ultimo evento sul life bit.
    istanteUltimaCommutazioneLifeBit = millis();

    // aggiorno lo stato del LED
    if (statoPinLifeBit == LOW) {
      statoPinLifeBit = HIGH;
    } else {
      statoPinLifeBit = LOW;
    }
  }

  // accendo il LED
  digitalWrite(pinLifeBit, statoPinLifeBit);

  //----------COMANDI MANUALI
  if (digitalRead(pinRelayOnManuale) == HIGH) {
    comandaRelay(pinRelayOn);
    comandoManuale = "ON";
    delay(100);  // Debounce bottone
  }
  if (digitalRead(pinRelayOffManuale) == HIGH) {
    comandaRelay(pinRelayOff);
    comandoManuale = "OFF";
    delay(100);  // Debounce bottone
  }

  //----------LEGGO LA CORRENTE AL MOTORE
  // Il sensore ACS712 da 20A presenta le seguenti caratteristiche:
  // - Tensione @ 0A = Vcc/2
  // - Fattore di scala: 100mV/A, ossia 0.1V/A

  // Arduino ha una risuluzione di 10bit per cui restituisce valori compresi tra 0 e 1023 per le corrispettive tensioni tra 0 e 5V.
  // Innanzitutto leggo la tensione di output del sensore in volt: è pari al numero letto dal sensore, diviso per 1024 e moltiplicato per 5! Ossia moltiplicato per 0.0048828125.
  int letturaPinCorrente = analogRead(pinLetturaCorrente);
  
  double tensioneOutputSensore = (double)letturaPinCorrente / 1024 * 5;

  // Converto la tensione di uscite del sensore nel valore di corrente che attraversa il motore. NOTA: si tratta di un valore ISTANTANEO!
  double correnteMotore = (tensioneOutputSensore - valorZeroPinCorrenteMotore) / 0.1;

  //----------CONTROLLO ACCENSIONE MOTORE
  durataAccensioneMotore = analizzaStatoGrandezzaSinusoidale(ultimoStatoNotoMotore, correnteMotore, sogliaCorrenteMotoreOn, istanteAccensioneMotore, periodoMinimoMotoreOff, primoIstanteSottoSogliaMotore);

  // if (ultimoStatoNotoMotore == "OFF") {
  //   // Ultimo stato noto: SPENTO!
  //   // Devo ancora controllare se la corrente è sopra o sotto la soglia!
  //   if (abs(correnteMotore) >= sogliaCorrenteMotoreOn) {
  //     // Il motore si è acceso!
  //     ultimoStatoNotoMotore = "ON";
  //     istanteAccensioneMotore = millis();
  //   }
  // } else if (ultimoStatoNotoMotore == "ON") {
  //   // Ultimo stato noto: ACCESO!

  //   // VERIFICA SPEGNIMENTO REGOLARE
  //   // Devo quindi verificare che la lettura di corrente sia stabile sotto la
  //   // soglia almeno per la durata del periodo minimo!
  //   if (abs(correnteMotore) < sogliaCorrenteMotoreOn) {
  //     // Il motore POTREBBE essere spento
  //     if (not trovatoIstanteSottoSogliaMotoreOn) {
  //       // Ho trovato il primo istante!
  //       primoIstanteSottoSoglia = millis();
  //       trovatoIstanteSottoSogliaMotoreOn = true;
  //     } else {
  //       // Avevo già trovato il primo istante, quindi devo controllare che sia
  //       // costante da un po' di tempo!
  //       if ((millis() - primoIstanteSottoSoglia) >= periodoMinimoMotoreOff) {
  //         // Il motore è effettivamente spento!
  //         durataUltimaAccensione = (millis() - istanteAccensioneMotore);
  //         ultimoStatoNotoMotore = "OFF";
  //         trovatoIstanteSottoSogliaMotoreOn = false;
  //         inviareUltimaAccensione = true;
  //       }
  //     }
  //   } else {
  //     // Il motore è acceso!
  //     // NOTA: può capitare che io trovi delle acquisizioni sotto la soglia minima e poi tornino sopra: significa che sto leggendo i punti della sinusoide in cui vado vicino allo zero!
  //     // In questo caso, però, devo pulire le variabili!
  //     ultimoStatoNotoMotore = "ON";
  //     trovatoIstanteSottoSogliaMotoreOn = false;
  //   }
  // }

  //----------SPEGNIMENTO DI SICUREZZA MOTORE
  // Devo controllare che non sia acceso oltre il periodo massimo consentito!
  if ((ultimoStatoNotoMotore == "ON") and (millis() - istanteAccensioneMotore) >= periodoMassimoAccensione) {
    comandaRelay(pinRelayOff);
    inviareSpegnimentoDiSicurezza = true;
    // durataUltimaAccensione = (millis() - istanteAccensioneMotore);
    // ultimoStatoNotoMotore = "OFF";
    // trovatoIstanteSottoSogliaMotoreOn = false;
    // inviareUltimaAccensione = true;
  }

  //----------CONTROLLO ACCENSIONE SPIA ALLARME
  int tensioneAllarme = analogRead(pinLetturaTensione);

  durataAccensioneAllarme = analizzaStatoGrandezzaSinusoidale(ultimoStatoNotoAllarme, (double) tensioneAllarme , sogliaTensioneAllarmeOn, istanteAccensioneAllarme, periodoMinimoAllarmeOff, primoIstanteSottoSogliaAllarme);


  //----------GESTIONE SERIALE
  if (Serial) {

    // Mando le informazioni che devo inviare in ogni caso!
    if (inviareSpegnimentoDiSicurezza) {
      Serial.println("ATTENZIONE! E' stato eseguito uno spegnimento di sicurezza!");
      inviareSpegnimentoDiSicurezza = false;
    }
    if (durataAccensioneMotore > 0 ) {
      Serial.println("Il motore ha eseguito un ciclo di " + String((float)durataAccensioneMotore / 1000) + " s.");
      durataAccensioneMotore = 0;
    }
    // if (inviareUltimaAccensione > 0 ) {
    //   Serial.println("Il motore ha eseguito un ciclo di " + String((float)durataUltimaAccensione / 1000) + " s.");
    //   inviareUltimaAccensione = false;
    // }

    if (ultimoStatoNotoAllarme == "ON"){
      Serial.println("Allarme aspirapellet attivo!");
    }

    if (durataAccensioneAllarme > 0) {
      Serial.println("L'allarme è rimasto acceso per " + String((float)durataAccensioneAllarme / 1000) + " s.");
      durataAccensioneMotore = 0;
    }

    if (comandoManuale != "") {
      Serial.println("E' stato eseguito il seguente comando manuale: " + comandoManuale);
    }

    // Gestione comunicazione seriale
    while (Serial.available()) {
      
      // Leggo il comando
      String comandoSeriale = String(Serial.readString());
      delay(10);
      comandoSeriale.trim();
      comandoSeriale.toUpperCase();


      // HELP
      if (comandoSeriale == "HELP") {
        Serial.println("Comandi disponibili:");
        Serial.println("\t- ON: chiude il relay principale.");
        Serial.println("\t- OFF: apre il relay principale.");
        Serial.println("\t- RECORD: registra le letture di corrente.");
        Serial.println("\t- STATO: ultimo stato noto del motore.");
        Serial.println("\t- CORRENTE: corrente del motore.");
        Serial.println("\t- VOLT: tensione sensore corrente.");
        Serial.println("\t- LETTURA: lettura sensore corrente.");
        Serial.println("\t- MISURA PELLET: lettura sensore ultrasuoni.");
        Serial.println("\t- LIVELLO PELLET: percentuale riempimento tanica di pellet.\n");
      }

      // Gestione info motore
      if (comandoSeriale == "STATO") {
        Serial.println("Ultimo stato noto del motore: " + ultimoStatoNotoMotore);
      } else if (comandoSeriale == "CORRENTE") {
        Serial.println("Corrente motore: " + String(correnteMotore));
      } else if (comandoSeriale == "VOLT") {
        Serial.println("Tensione al sensore: " + String(tensioneOutputSensore));
      } else if (comandoSeriale == "LETTURA") {
        Serial.println("Lettura sensore: " + String(letturaPinCorrente));
      }

      // Gestione ON, OFF, RECORD
      //abilitaRecord = false;
      if (comandoSeriale == "OFF") {
        comandaRelay(pinRelayOff);
        Serial.println("Eseguito comando seriale OFF");
      } else if (comandoSeriale == "ON") {
        comandaRelay(pinRelayOn);
        Serial.println("Eseguito comando seriale ON");
      } else if (comandoSeriale == "RECORD") {
        // Abilito la porzione di codice di registrazione
        //abilitaRecord = true;
        //Serial.println("Inizio acquisizione letture sensore di corrente.");
        RegistraGrandezze(pinLetturaCorrente, pinLetturaTensione);
      }

      // Gestione livello pelletstato
      if (comandoSeriale == "ALTEZZA PELLET"){
        // NOTA: si intende la distanza del pellet dal sensore!
        float altezzaPellet_cm = MisuraAltezzaPellet_cm();
        Serial.println("Altezza pellet dal sensore [cm]: " + (String)altezzaPellet_cm);
      }
      if (comandoSeriale == "LIVELLO PELLET"){
        int percLivelloPellet = MisuraLivelloPellet();
        Serial.println("Livello pellet [%]: " + percLivelloPellet);
      }

    }
  }



  // //----------GESTIONE RECORD
  // if (abilitaRecord) {
  //   // conto quante iterazioni devo fare
  //   byte numeroIterazioni = 0;
  //   numeroIterazioni = sizeof(acquisizioni) - 1;

  //   // Acquisisco i dati
  //   if ((millis() - istanteUltimaAcquisizione) >= (1 / (float)frequezaAcquisizione_Hz)) {


  //     // aggiorno l'istante dell'ultima acquisizione
  //     istanteUltimaAcquisizione = millis();

  //     // acquisisco un punto
  //     if (contAcquisizioni < (int)numeroIterazioni) {
  //       istantiAcquisizioni[contAcquisizioni] = millis();
  //       acquisizioni[contAcquisizioni] = analogRead(pinLetturaCorrente);
  //       contAcquisizioni++;
  //     }
  //   }

  //   // Controllo di essere arrivato al fondo e le stampo!
  //   if (contAcquisizioni >= (int)numeroIterazioni) {
  //     if (Serial) {
  //       for (byte i = 0; i < numeroIterazioni; i++) {          
  //         Serial.println(String(istantiAcquisizioni[i]) + ";" + String(acquisizioni[i]));
  //         delay(10);
  //       }
  //       // dopo aver stampato, azzero il contatore e disabilito l'acuqisizione
  //       contAcquisizioni = 0;
  //       abilitaRecord = false;
  //       Serial.println("Fine acquisizione letture sensore di corrente.");
  //     }
  //   }
  // }
}
