#include "encoder/encoder_trainer.h"
#include "embedding/vocab.h"
#include "storage/segment_reader.h"
#include <iostream>
#include <filesystem>

int main(int argc, char** argv) {
    std::string segdir = "../segments/seg_000001";
    std::string embeddir = "../embeddings";
    int epochs = 10;
    double lr = 3e-4;
    int64_t dim = 128;

    // Parse optional args
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--epochs" && i + 1 < argc) epochs = std::stoi(argv[++i]);
        else if (arg == "--lr" && i + 1 < argc) lr = std::stod(argv[++i]);
        else if (arg == "--dim" && i + 1 < argc) dim = std::stoi(argv[++i]);
        else if (arg == "--segdir" && i + 1 < argc) segdir = argv[++i];
        else if (arg == "--embeddir" && i + 1 < argc) embeddir = argv[++i];
    }

    std::filesystem::create_directories(embeddir);

    std::cerr << "Loading segment from " << segdir << "...\n";
    SegmentReader reader(segdir);

    std::cerr << "Building vocabulary...\n";
    Vocabulary vocab;
    vocab.build_from_terms(reader.all_terms());
    vocab.save(embeddir + "/vocab.txt");
    std::cerr << "Vocab size: " << vocab.size() << "\n";

    std::cerr << "Initializing encoder (dim=" << dim << ")...\n";
    EncoderTrainer trainer(vocab, dim, /*heads=*/4, /*layers=*/2, /*max_len=*/256);

    std::cerr << "Training for " << epochs << " epochs...\n";
    trainer.train(reader, epochs, lr, /*batch_size=*/4);

    std::string model_path = embeddir + "/encoder.pt";
    trainer.save(model_path);
    std::cerr << "Model saved to " << model_path << "\n";

    // Quick test: encode a sample query
    auto emb = trainer.encode("what is a database");
    std::cerr << "Sample embedding dim=" << emb.size() << " [";
    for (size_t i = 0; i < std::min<size_t>(5, emb.size()); i++)
        std::cerr << emb[i] << (i + 1 < emb.size() ? ", " : "");
    std::cerr << "...]\n";

    std::cerr << "Done.\n";
    return 0;
}
