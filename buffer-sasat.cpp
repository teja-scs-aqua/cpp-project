#include <iostream>
#include <cstring>

// Function containing a buffer overflow vulnerability
void vulnerableFunction() {
    char buffer[10]; // Fixed-size buffer
    
    std::cout << "Enter input: ";
    std::cin >> buffer; // No bounds checking - potential buffer overflow
    
    std::cout << "You entered: " << buffer << std::endl;
}

int main() {
    vulnerableFunction();
    return 0;
}
