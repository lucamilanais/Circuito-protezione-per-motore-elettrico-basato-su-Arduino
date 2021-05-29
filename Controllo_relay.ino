/*
  Controllo relay

  Questo sketch serve per controllare un relay tramite comunicazione seriale.
  Si prevede di poter inviare un comando di ON, OFF e BLINK.

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

  
  //----------LEGGO IL SENSORE
  int letturaCorrente = analogRead(pinLetturaCorrente);

  
  //----------GESTIONE SERIALE
  // Leggo il comando
  if (Serial) {
    while (Serial.available()) {
      comando = String(Serial.readString());
      comando.toUpperCase();
      delay(10);

      Serial.println("Ho ricevuto: " + comando);
      //Serial.println("Ho ricevuto: " + comando + ", il risultato di indexOf('ON') è: " + comando.indexOf("ON"));
      delay(10);
      
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
