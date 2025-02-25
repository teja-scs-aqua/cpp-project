#include <iostream>
#include <cstring>

void vulnerableFunction() {
    char buffer[10];  // Fixed-size buffer (10 bytes)

    std::cout << "Enter input: ";
    std::cin >> buffer;  // No bounds checking - possible buffer overflow

    std::cout << "You entered: " << buffer << std::endl;
}

void anotherVulnerableFunction() {
    char smallBuffer[5];  
    const char *largeInput = "ThisIsALongInputString";

    // Dangerous: strcpy does not check bounds
    strcpy(smallBuffer, largeInput); // Buffer overflow risk

    std::cout << "Buffer contains: " << smallBuffer << std::endl;
}

int main() {
    vulnerableFunction();
    anotherVulnerableFunction();
    return 0;
}
