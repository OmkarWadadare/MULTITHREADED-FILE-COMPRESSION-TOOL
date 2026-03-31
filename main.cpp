// simple_multithreaded_compressor.cpp

#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <zlib.h>
#include <chrono>

const size_t CHUNK_SIZE = 1024 * 1024; // 1MB

struct CompressedChunk {
    std::vector<unsigned char> data;
    size_t original_size;
};

std::mutex mtx;

// ------------------- Compression Worker -------------------

void compress_chunk(const std::vector<unsigned char>& input,
                    std::vector<CompressedChunk>& output,
                    int index) {

    uLongf compressed_size = compressBound(input.size());
    std::vector<unsigned char> compressed_data(compressed_size);

    if (compress(compressed_data.data(), &compressed_size,
                 input.data(), input.size()) != Z_OK) {
        std::cerr << "Compression failed\n";
        return;
    }

    compressed_data.resize(compressed_size);

    std::lock_guard<std::mutex> lock(mtx);
    output[index] = {compressed_data, input.size()};
}

// ------------------- Compress File -------------------

void compress_file(const std::string& input_file,
                   const std::string& output_file,
                   size_t& original_size,
                   size_t& compressed_size_total) {

    std::ifstream file(input_file, std::ios::binary);
    if (!file) {
        std::cerr << "Error opening input file\n";
        return;
    }

    std::vector<std::vector<unsigned char>> chunks;

    while (true) {
        std::vector<unsigned char> buffer(CHUNK_SIZE);
        file.read((char*)buffer.data(), CHUNK_SIZE);
        std::streamsize bytesRead = file.gcount();

        if (bytesRead <= 0) break;

        buffer.resize(bytesRead);
        chunks.push_back(buffer);
        original_size += bytesRead;
    }

    std::vector<CompressedChunk> compressed_chunks(chunks.size());

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (size_t i = 0; i < chunks.size(); ++i) {
        threads.emplace_back(compress_chunk,
                             std::ref(chunks[i]),
                             std::ref(compressed_chunks),
                             i);
    }

    for (auto& t : threads) t.join();

    auto end = std::chrono::high_resolution_clock::now();

    std::cout << "Compression Time: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
              << " ms\n";

    std::ofstream out(output_file, std::ios::binary);

    size_t num_chunks = compressed_chunks.size();
    out.write((char*)&num_chunks, sizeof(num_chunks));

    for (auto& chunk : compressed_chunks) {
        size_t comp_size = chunk.data.size();
        compressed_size_total += comp_size;

        out.write((char*)&comp_size, sizeof(comp_size));
        out.write((char*)&chunk.original_size, sizeof(chunk.original_size));
        out.write((char*)chunk.data.data(), comp_size);
    }
}

// ------------------- Decompress File -------------------

void decompress_file(const std::string& input_file,
                     const std::string& output_file) {

    std::ifstream in(input_file, std::ios::binary);
    if (!in) {
        std::cerr << "Error opening compressed file\n";
        return;
    }

    size_t num_chunks;
    in.read((char*)&num_chunks, sizeof(num_chunks));

    std::vector<std::vector<unsigned char>> output(num_chunks);

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < num_chunks; ++i) {
        size_t comp_size, orig_size;
        in.read((char*)&comp_size, sizeof(comp_size));
        in.read((char*)&orig_size, sizeof(orig_size));

        std::vector<unsigned char> comp_data(comp_size);
        in.read((char*)comp_data.data(), comp_size);

        output[i].resize(orig_size);

        uLongf dest_len = orig_size;
        if (uncompress(output[i].data(), &dest_len,
                       comp_data.data(), comp_size) != Z_OK) {
            std::cerr << "Decompression failed\n";
            return;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();

    std::cout << "Decompression Time: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
              << " ms\n";

    std::ofstream out(output_file, std::ios::binary);
    for (auto& chunk : output)
        out.write((char*)chunk.data(), chunk.size());
}

// ------------------- Main -------------------

int main() {
    std::string input = "input.txt";
    std::string compressed = "compressed.bin";
    std::string output = "output.txt";

    size_t original_size = 0;
    size_t compressed_size = 0;

    std::cout << "Starting Compression...\n";
    compress_file(input, compressed, original_size, compressed_size);

    std::cout << "Original Size: " << original_size << " bytes\n";
    std::cout << "Compressed Size: " << compressed_size << " bytes\n";

    if (original_size > 0) {
        double ratio = (double)compressed_size / original_size;
        std::cout << "Compression Ratio: " << ratio << "\n";
        std::cout << "Space Saved: " << (1 - ratio) * 100 << "%\n";
    }

    std::cout << "\nStarting Decompression...\n";
    decompress_file(compressed, output);

    std::cout << "\nDone. Check output.txt\n";

    return 0;
}