/*

	bin2text, written in C++

	with this program you can turn a binary file into a hex array to be used in c/c++ programs

	compile this with g++, and then run it from the command line	
	usage is as follows:
	./a.out <PATH_TO_BINARY> <PATH_TO_TEXTFILE> <NUMBER_OF_BYTES_PER_ROW_PRINTED> <NAME_OF_ARRAY_AND_ARRAY_SIZE_INT>

*/

#include <iostream>
#include <fstream>
#include <string>
using namespace std;

bool IntTryParse(string str, int *int_out) 
{
	if (str.empty()) {
		return false;
	}
    try {
        *int_out = stoi(str);
		return true;
    }
    catch(...) {
        return false;
    }
}
 
int main(int argc, char **argv)
{
    if (argc != 5) { 
        cout << ((argc > 5) ? "Too many arguments! Exiting...\n" : "Not enough arguments! Exiting...\n");
        return 0;
    }

	int horizontal;
    if (!IntTryParse(argv[3], &horizontal)) {
		cout << "Failed to parse number of bytes! Exiting..." << endl;
		return 0;
	}

	ifstream FileIn(argv[1], ios::in | ios::binary | ios::ate);
	if (!FileIn.is_open()) {
		cout << "Unable to open binary! Exiting...\n";
		return 0;
	}

	int filesize = FileIn.tellg(); //this is the filesize from 1 to n, NOT 0 to n
	char *ReadFile = new char[filesize];
	FileIn.seekg(0);
	FileIn.read(ReadFile, filesize);
	FileIn.close();

	ofstream FileOut(argv[2]);
	if (!FileOut.is_open()) {
		cout << "Unable to open text file! Exiting...\n";
		return 0;
	}

	FileOut << "u8 " << argv[4] << "[] = {" << endl;
	for (int i = 0; i < filesize; i++) {
		int num_to_print = int(static_cast<unsigned char>(ReadFile[i]));
		if (num_to_print < 16) {
			FileOut << hex << uppercase << "0x0" << num_to_print << ((i == filesize - 1) ? " " : ", "); //if the current byte is less than 16, then you have to put a 0 in front of the number because otherwise it would print e.g. "0xA" instead of "0x0A" or "0x0" instead of "0x00"
		}
		else {	
			FileOut << hex << uppercase << "0x" << num_to_print << ((i == filesize - 1) ? " " : ", "); //in all other cases however you can do this normally
		}
		if ((i + 1) % horizontal == 0 && i != 0) { 
			FileOut << endl; //and here we break a new line if we get to the correct number of bytes printed
		}
	}
	FileOut << endl << "};" << endl;
	FileOut << "int " << argv[4] << "_size" << " = " << hex << "0x" << filesize << ";";
	FileOut.close();

	cout << "Done!\n";	

    return 0;
}