#include <iostream>  // Librería para imprimir en consola
#include <thread>    // Librería para usar hilos
#include <semaphore> // Librería para usar semáforos
#include <queue>     // Librería para usar colas (queue)
#include <vector>    // Librería para usar vectores
#include <chrono>    // Librería para manipular tiempo
#include <mutex>     // Librería para usar mutex (para evitar condiciones de carrera)
#include <fstream>   // Librería para manejar archivos
#include <sstream>   // Librería para construir cadenas de texto

using namespace std;

// Variables globales para configuración
int N;    // Número de ítems que produce cada productor y consume cada consumidor
int NP;   // Número de productores
int NC;   // Número de consumidores
const int MAX_WAIT_TIME_MS = 5000;  // Tiempo máximo de espera para consumidores (en milisegundos)
const int PRODUCER_RETRY_DELAY_MS = 500;  // Tiempo de espera para que un productor vuelva a intentar insertar si el buffer está lleno

// Archivo de salida para guardar los logs
ofstream logFile("producer-consumer.txt");  
std::mutex print_mutex;  // Mutex para sincronizar impresiones y evitar conflictos

// Función para imprimir y escribir en archivo 
void printMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(print_mutex);  // Bloquear el mutex para asegurar exclusividad en las impresiones
    cout << message;  // Imprimir en la consola
    logFile << message;  // Escribir en el archivo
}

class Buffer {
private:
    queue<int> buffer;  // Cola que representa el buffer compartido
    counting_semaphore<1> buffer_mutex{1};   // Semáforo para sincronizar el acceso al buffer
    counting_semaphore<> spaces;       // Semáforo que indica los espacios disponibles en el buffer
    counting_semaphore<> items{0};     // Semáforo que indica cuántos ítems hay en el buffer para consumir

public:
    // Constructor que inicializa el semáforo `spaces` con la capacidad del buffer
    Buffer(int capacity) : spaces(capacity) {}  

    // Método para manejar el caso cuando el productor está esperando para insertar el ítem
    void notifyProducerWait(int id, int item) {
        std::lock_guard<std::mutex> lock(print_mutex);  // Bloquear el mutex para que la impresión sea atómica
        cout << "Error de inserción - buffer lleno. El productor " << id << " está esperando para insertar el ítem " << item << endl;
        logFile << "Error de inserción - buffer lleno. El productor " << id << " está esperando para insertar el ítem " << item << endl; // Escribir en el archivo
    }

    // Método para manejar el caso cuando el consumidor espera demasiado tiempo
    void handleConsumerTimeout(int id) {
        std::lock_guard<std::mutex> lock(print_mutex);  // Bloquear el mutex para que la impresión sea atómica
        cout << "Error del consumidor " << id << ": Buffer vacío, el consumidor esperó demasiado tiempo." << endl;  // Mensaje de error
        logFile << "Error del consumidor " << id << ": Buffer vacío, el consumidor esperó demasiado tiempo." << endl;  // Escribir en el archivo
    }

    // Método para que un productor añada un ítem al buffer
    void produce(int id, int item) {
        while (true) { // Bucle infinito hasta que se produzca la inserción
            // Intenta adquirir un espacio en el buffer con un tiempo de espera
            if (!spaces.try_acquire_for(chrono::milliseconds(PRODUCER_RETRY_DELAY_MS))) {
                notifyProducerWait(id, item);  // Llama al método para manejar la espera del productor
            } else {
                buffer_mutex.acquire(); // Adquiere el semáforo para acceder al buffer
                buffer.push(item); // Inserta el ítem en el buffer
                std::stringstream ss;  // Crear un stringstream para construir el mensaje
                ss << "Inserción exitosa" << endl;  // Mensaje de inserción exitosa
                ss << "Productor " << id << " produjo: " << item << "\n"; // Mensaje del productor
                printMessage(ss.str()); // Llama a la función para imprimir y escribir en el archivo
                buffer_mutex.release(); // Libera el semáforo después de modificar el buffer
                items.release(); // Indica que hay un nuevo ítem disponible
                break; // Sale del bucle después de la inserción
            }
        }
    }

    // Método para que un consumidor tome un ítem del buffer
    int consume(int id) {
        // Intenta adquirir un ítem con un tiempo de espera
        if (!items.try_acquire_for(chrono::milliseconds(MAX_WAIT_TIME_MS))) {
            handleConsumerTimeout(id);  // Llama al método para manejar el timeout del consumidor
            return -1; // Retorna -1 si no pudo consumir
        }

        buffer_mutex.acquire(); // Adquiere el semáforo para acceder al buffer
        int item = buffer.front(); // Obtiene el ítem en la parte frontal del buffer
        buffer.pop(); // Elimina el ítem del buffer
        std::stringstream ss;  // Crear un stringstream para construir el mensaje
        ss << "Consumidor " << id << " consumió: " << item << "\n"; // Mensaje de consumo
        printMessage(ss.str()); // Llama a la función para imprimir y escribir en el archivo
        buffer_mutex.release(); // Libera el semáforo después de modificar el buffer
        spaces.release(); // Indica que hay un espacio disponible en el buffer
        return item; // Retorna el ítem consumido
    }

    // Método para mostrar los ítems restantes en el buffer
    void showRemainingItems() {
        buffer_mutex.acquire(); // Adquiere el semáforo para acceder al buffer
        std::stringstream ss;  // Crear un stringstream para construir el mensaje
        ss << "Elementos restantes en el buffer: "; // Mensaje de inicio
        if (buffer.empty()) { // Verifica si el buffer está vacío
            ss << "El buffer está vacío.\n"; // Mensaje si el buffer está vacío
        } else {
            queue<int> temp = buffer; // Copia temporal del buffer
            while (!temp.empty()) { // Recorre la copia temporal
                ss << temp.front() << " "; // Agrega cada ítem al mensaje
                temp.pop(); // Elimina el ítem de la copia
            }
            ss << "\n"; // Salto de línea al final
        }
        printMessage(ss.str()); // Llama a la función para imprimir y escribir en el archivo
        buffer_mutex.release(); // Libera el semáforo después de mostrar los ítems
    }
};

// Clase Productor
class Producer {
private:
    int id; // Identificador del productor
    Buffer& buffer; // Referencia al buffer compartido

public:
    // Constructor que inicializa el identificador y la referencia al buffer
    Producer(int id, Buffer& buffer) : id(id), buffer(buffer) {}

    // Sobrecarga del operador () para que la clase se pueda usar como un hilo
    void operator()() {
        {
            std::stringstream ss;  // Crear un stringstream para construir el mensaje
            ss << "Productor " << id << " creado.\n"; // Mensaje de creación del productor
            printMessage(ss.str()); // Llama a la función para imprimir y escribir en el archivo
        }

        // Bucle para producir N ítems
        for (int i = 0; i < N; ++i) {
            int item = id * 100 + i; // Generar un ítem único basado en el id del productor
            buffer.produce(id, item); // Llama al método para producir el ítem en el buffer
            this_thread::sleep_for(chrono::milliseconds(2000)); // Espera 2 segundos entre producciones
        }

        {
            std::stringstream ss;  // Crear un stringstream para construir el mensaje
            ss << "Productor " << id << " ha terminado.\n"; // Mensaje de finalización del productor
            printMessage(ss.str()); // Llama a la función para imprimir y escribir en el archivo
        }
    }
};

// Clase Consumidor
class Consumer {
private:
    int id; // Identificador del consumidor
    Buffer& buffer; // Referencia al buffer compartido

public:
    // Constructor que inicializa el identificador y la referencia al buffer
    Consumer(int id, Buffer& buffer) : id(id), buffer(buffer) {}

    // Sobrecarga del operador () para que la clase se pueda usar como un hilo
    void operator()() {
        {
            std::stringstream ss;  // Crear un stringstream para construir el mensaje
            ss << "Consumidor " << id << " creado.\n"; // Mensaje de creación del consumidor
            printMessage(ss.str()); // Llama a la función para imprimir y escribir en el archivo
        }

        // Bucle para consumir N ítems
        for (int i = 0; i < N; ++i) {
            int item = buffer.consume(id); // Llama al método para consumir un ítem del buffer
            if (item != -1) { // Verifica si el ítem fue consumido correctamente
                this_thread::sleep_for(chrono::milliseconds(1500)); // Espera 1.5 segundos entre consumos
            }
        }

        {
            std::stringstream ss;  // Crear un stringstream para construir el mensaje
            ss << "Consumidor " << id << " ha terminado.\n"; // Mensaje de finalización del consumidor
            printMessage(ss.str()); // Llama a la función para imprimir y escribir en el archivo
        }
    }
};

// Clase Principal para ejecutar el programa
class Principal {
private:
    Buffer buffer; // Instancia del buffer
    vector<thread> producers; // Vector para almacenar los hilos de productores
    vector<thread> consumers; // Vector para almacenar los hilos de consumidores

public:
    // Constructor que inicializa el buffer con la capacidad proporcionada
    Principal(int capacity) : buffer(capacity) {}

    // Método para ejecutar la lógica principal
    void run() {
        // Crear hilos para los productores
        for (int i = 0; i < NP; ++i) {
            producers.emplace_back(Producer(i + 1, buffer)); // Agrega un nuevo hilo productor
        }

        // Crear hilos para los consumidores
        for (int i = 0; i < NC; ++i) {
            consumers.emplace_back(Consumer(i + 1, buffer)); // Agrega un nuevo hilo consumidor
        }

        // Unir todos los hilos de productores
        for (auto& p : producers) {
            p.join(); // Espera a que cada productor termine
        }

        // Unir todos los hilos de consumidores
        for (auto& c : consumers) {
            c.join(); // Espera a que cada consumidor termine
        }

        buffer.showRemainingItems(); // Muestra los ítems restantes en el buffer
    }
};

// Función principal
int main(int argc, char* argv[]) {
    // Verifica que se proporcionen los parámetros correctos
    if (argc != 5) {
        cout << "Uso: " << argv[0] << " <capacidad del buffer> <número de ítems> <número de productores> <número de consumidores>" << endl;
        return 1; // Retorna 1 si el número de argumentos es incorrecto
    }

    // Convierte los argumentos de la línea de comandos a enteros
    int buffer_capacity = atoi(argv[1]);
    N = atoi(argv[2]);
    NP = atoi(argv[3]);
    NC = atoi(argv[4]);

    // Verifica que todos los parámetros sean números positivos
    if (buffer_capacity <= 0 || N <= 0 || NP <= 0 || NC <= 0) {
        cerr << "Todos los parámetros deben ser números positivos.\n"; // Mensaje de error
        return 1; // Retorna 1 si algún parámetro es no válido
    }

    Principal principal(buffer_capacity); // Crea una instancia de la clase Principal con la capacidad del buffer
    principal.run(); // Ejecuta el método run de la clase Principal

    logFile.close();  // Cerrar el archivo de log
    return 0; // Retorna 0 para indicar que el programa terminó correctamente
}
