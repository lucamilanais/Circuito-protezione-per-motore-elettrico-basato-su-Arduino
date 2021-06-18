/*
  Controllo relay

  Questo sketch serve per controllare un relay di protenzione a un motore.

  NOTA: si utilizza il LED integrato nella scheda come "life bit"

*/

//--------------------VARIABILI GLOBALI
// Dichiaro i numeri dei pin di interfacciamento
const int pinLifeBit =  LED_BUILTIN;  // Pin di default: è il pin 13
const int pinRelayOn = 5;
const int pinRelayOff = 6;
const int pinLetturaCorrente = A0;

//inizializzo i pin
int statoPinLifeBit = LOW;
int statoPinRelayOn = LOW;
int statoPinRelayOff = LOW;

//life bit
unsigned long istantePrecedenteLifeBit = 0;
const long intervalloLifeBit = 1000;

//blink
bool abilitaBlink = false;
unsigned long istantePrecedenteBlinkRelay = 0;
const long intervalloBlinkRelay = 500;
String ultimoStatoNotoRelay = "OFF";

//Record
bool abilitaRecord = false;
unsigned long istantePrecedenteRecord = 0;
unsigned long istantiAcquisizioni[200];
int acquisizioni[200];
int contAcquisizioni = 0;


//comando relay
const int delayComandoRelay = 50;


//controllo periodo accensione
String ultimoStatoNotoMotore = "OFF";
float sogliaCorrenteMotoreOn = 0.5;    //Valore oltre il quale assumo stia passando corrente nel motore e che, quindi, sia acceso.
float periodoMinimoMotoreOff = 100;    //Periodo minimo in cui la lettura di corrente che passa al motore deve risultare sotto la soglia affichè possa assumere che si sia spento: due periodi alla frequenza di rete (50Hz)! 
float istanteAccensioneMotore = 0;
float durataUltimaAccensione = 0;
bool trovatoIstanteSottoSogliaMotoreOn = false;
float primoIstanteSottoSoglia = 0;
bool inviareUltimaAccensione = false;
float periodoMassimoAccensione = 30000;
bool inviareSpegnimentoDiSicurezza = false;





//--------------------FUNZIONI SUPPORTO
void ComandaRelay(int numeroPin){
  //Questa procedura comanda il relay attivando il pin relativo.
  //ATTENZIONE: il relay ha bisogn odi un impulso di almeno 50ms, qundi si deve riportare il pina zero.

  digitalWrite(numeroPin,HIGH);
  delay(delayComandoRelay);
  digitalWrite(numeroPin,LOW);
  
}






//--------------------SETUP
void setup() {
  // Imposto i vari pin e apro la comunicazione seriale.
  pinMode(pinLifeBit, OUTPUT);
  pinMode(pinRelayOn, OUTPUT);
  pinMode(pinRelayOff, OUTPUT);

  pinMode(pinLetturaCorrente, INPUT);
  
  Serial.begin(9600);
}




//--------------------LOOP
void loop() {
  
  //----------DICHIARAZIONE VARIABILI
  unsigned long istanteAttuale = millis();
  String comando = "";
  
  //----------GESTIONE LIFE BIT
  if (istanteAttuale - istantePrecedenteLifeBit >= intervalloLifeBit) {
    // aggiorno l'istante dell'ultimo evento sul life bit.
    istantePrecedenteLifeBit = istanteAttuale;

    // aggiorno lo stato del LED
    if (statoPinLifeBit == LOW) {
      statoPinLifeBit = HIGH;
    } else {
      statoPinLifeBit = LOW;
    }
  }
  
  // accendo il LED
  digitalWrite(pinLifeBit, statoPinLifeBit);


  
  //----------LEGGO LA CORRENTE AL MOTORE
  //Il sensore ACS712 da 20A presenta le seguenti caratteristiche:
  // - Tensione @ 0A = Vcc/2
  // - Fattore di scala: 100mV/A, ossia 0.1V/A
  
  //Arduino ha una risuluzione di 10bit er cui restituisce valori compresi tra 0 e 1023 per le corrispettive tensioni tra 0 e 5V.
  //Innanzitutto leggo la tensione di output del sensore in volt: è pari al numero letto dal sensore, diviso per 1024 e moltiplicato per 5! Ossia moltiplicato per 0.0048828125
  int letturaPinCorrente = analogRead(pinLetturaCorrente);
  double tensioneOutputSensore = letturaPinCorrente * 0.0048828125;
  

  //Converto la tensione di uscite del sensore nel valore di corrente che attraversa il motore.
  //NOTA: si tratta di un valore ISTANTANEO!
  double correnteMotore = (tensioneOutputSensore - 2.5)/0.1;




  //----------CONTROLLO ACCENSIONE MOTORE
  if (ultimoStatoNotoMotore == "OFF") {
    // Ultimo stato noto: SPENTO!
    //Devo ancora controllare se la corrente è sopra o sotto la soglia!
    if (abs(correnteMotore) >= sogliaCorrenteMotoreOn) {
      //Il motore si è acceso!
      ultimoStatoNotoMotore = "ON";
      istanteAccensioneMotore = millis();
    }
  } 
  else if (ultimoStatoNotoMotore == "ON") {
    // Ultimo stato noto: ACCESO!

    // VERIFICA SPEGNIMENTO REGOLARE
    // Devo quindi verificare che la lettura di corrente sia stabile sotto la soglia almeno per la durata del periodo minimo!
    if (abs(correnteMotore) < sogliaCorrenteMotoreOn) {
      // Il motore POTREBBE essere spento
      if (not trovatoIstanteSottoSogliaMotoreOn) {
        //Ho trovato il primo istante!
        primoIstanteSottoSoglia = millis();
        trovatoIstanteSottoSogliaMotoreOn = true;        
      }
      else {
        //Avevo già trovato il primo istante, quindi devo controllare che sia costante da un po' di tempo!
        if ((millis() - primoIstanteSottoSoglia) >= periodoMinimoMotoreOff) {
          // Il motore è effettivamente spento!
          durataUltimaAccensione = (millis() - istanteAccensioneMotore);
          ultimoStatoNotoMotore = "OFF";
          trovatoIstanteSottoSogliaMotoreOn = false;
          inviareUltimaAccensione = true;

        }
      }
    }
    else {
      // Il motore è acceso!
      // NOTA: può capitare che io trovi delle acquisizioni sotto la soglia minima e poi tornino sopra: significa che sto leggendo i punti della sinusoide in cui vado vicino allo zero!
      // In questo caso, però, devo pulire le variabili!
      ultimoStatoNotoMotore = "ON";
      trovatoIstanteSottoSogliaMotoreOn = false;
    }




  }


  
  //----------SPEGNIMENTO DI SICUREZZA MOTORE
  // Devo controllare che non sia acceso oltre il periodo massim consentito!
  if ((ultimoStatoNotoMotore == "ON") and (millis() - istanteAccensioneMotore) >= periodoMassimoAccensione) {
    ComandaRelay(pinRelayOff);
    inviareSpegnimentoDiSicurezza = true;
    durataUltimaAccensione = (millis() - istanteAccensioneMotore);
    ultimoStatoNotoMotore = "OFF";
    trovatoIstanteSottoSogliaMotoreOn = false;
    inviareUltimaAccensione = true;      
  }
  

  
  //----------GESTIONE SERIALE
  if (Serial) {

    //Mando le informazioni che devo inviare in ogni caso!

    if (inviareSpegnimentoDiSicurezza) {
      Serial.println("ATTENZIONE! E' stato eseguito uno spegnimento di sicurezza!");
      inviareSpegnimentoDiSicurezza = false;      
    }
    if (inviareUltimaAccensione) {
      Serial.println("Il motore ha eseguito un ciclo di " + String(durataUltimaAccensione / 1000) + " s.");
      inviareUltimaAccensione = false;
    }   
    
    // Leggo il comando
    while (Serial.available()) {
      
      comando = String(Serial.readString());
      comando.trim();
      comando.toUpperCase();
      delay(10);

      Serial.println("Ho ricevuto: " + comando);
      delay(10);
      


      //Gestione info motore
      if (comando.indexOf("STATO")>=0){
        Serial.println("Ultimo stato noto del motore: " + ultimoStatoNotoMotore);
      }
      else if (comando.indexOf("CORRENTE")>=0) {
        Serial.println("Corrente motore: "+ String(correnteMotore));
      }
      else if (comando.indexOf("VOLT")>=0) {
        Serial.println("Tensione sensore: "+ String(tensioneOutputSensore));
      }      
      else if (comando.indexOf("LETTURA")>=0) {
        Serial.println("Lettura sensore: "+ String(letturaPinCorrente));
      } 

            

      
      //Gestione ON, OFF, BLINK, RECORD
      abilitaRecord = false;
      if (comando.indexOf("OFF")>=0) {
        ComandaRelay(pinRelayOff);
        abilitaBlink = false;
      }
      else if (comando.indexOf("ON")>=0) {
        ComandaRelay(pinRelayOn);
        abilitaBlink = false;
      }
      else if (comando.indexOf("BLINK")>=0) {
        abilitaBlink = true;
      }
      else if (comando.indexOf("RECORD")>=0) {
        // Abilito la porzione di codice di registrazione
        abilitaRecord = true;
        
      }
      
    }
        
  }
  
  
  
  
  //----------GESTIONE BLINK
  if (abilitaBlink) {
    if (istanteAttuale - istantePrecedenteBlinkRelay >= intervalloBlinkRelay) {
      // aggiorno l'istante dell'ultimo evento sul life bit relay.
      istantePrecedenteBlinkRelay = istanteAttuale;
      
      if (ultimoStatoNotoRelay == "OFF") {
        ComandaRelay(pinRelayOn);
        ultimoStatoNotoRelay = "ON";
        
      } else {
        ComandaRelay(pinRelayOff);
        ultimoStatoNotoRelay = "OFF";
        
      }
    }    
  }

  //----------GESTIONE RECORD
  if (abilitaRecord) {
    // Acquisisco i dati
    if (istanteAttuale - istantePrecedenteRecord >= 1){
      // aggiorno l'istante dell'ultima acquisizione
      istantePrecedenteRecord = istanteAttuale;

      //acquisisco un punto
      contAcquisizioni++;
      if (contAcquisizioni<200) {
        istantiAcquisizioni[contAcquisizioni]=millis();
        acquisizioni[contAcquisizioni]=analogRead(pinLetturaCorrente);        
      }

      
      
    }

    //Controllo di essere arrivato al fondo e le stampo!
    if (contAcquisizioni >= 200) {
      if (Serial) {
        for (int i = 1; i < 200; i = i + 1) {
          Serial.println(String(istantiAcquisizioni[i]) + ";" + String(acquisizioni[i]));
          delay(10);
        }
        // dopo aver stampato, azzero il contatore e disabilito l'acuqisizione
        contAcquisizioni = 0;
        abilitaRecord = false;        
      }

    }
    
  }
  
}
