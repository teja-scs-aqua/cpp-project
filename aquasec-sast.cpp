# Vulnerable function to trigger high SAST issue in Aqua scan
void vulnerable_function() {
    char buffer[8]; // Fixed-size buffer (8 bytes)

    std::cout << "Enter input: ";
    std::cin >> buffer; // No bounds checking

    std::cout << "You entered: " << buffer << std::endl;
}

void vulnerable_function2 (int value) {
    switch (value) {
        case 1:
            std::cout << "case 1" << std::endl;
            break;
        case 2:
            std::cout << "case 2" << std::endl;
            break;
        case 3:
            std::cout << "case 3" << std::endl;
            break;
        // No default case!
    }
}
