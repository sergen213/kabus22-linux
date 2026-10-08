// Export an embedded PCX verbatim without modifying the Ghidra program.
// @category Kabus22
// Usage: ExportEmbeddedPcx.java OUTPUT_DIRECTORY IMAGE_ADDRESS [POINTER_ADDRESS [PE_FILE ...]]
// Headless: analyzeHeadless PROJECT_DIRECTORY PROJECT_NAME -process K22.exe -noanalysis -readOnly
//           -scriptPath SCRIPT_DIRECTORY -postScript ExportEmbeddedPcx.java OUTPUT_DIRECTORY 0062130c 00627310
// Single-plane images require the original 0x0c + 768-byte palette immediately after RLE.

import java.io.BufferedInputStream;
import java.io.InputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.util.HexFormat;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressOverflowException;
import ghidra.program.model.mem.MemoryAccessException;

public class ExportEmbeddedPcx extends GhidraScript {
    private static final long MAX_DECODED_BYTES = 64L * 1024 * 1024;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            throw new IllegalArgumentException(
                "Expected OUTPUT_DIRECTORY IMAGE_ADDRESS [POINTER_ADDRESS [PE_FILE ...]]");
        }
        if (currentProgram == null) {
            throw new IllegalStateException("An open program is required");
        }
        Address image = toAddr(args[1]);
        if (image == null) {
            throw new IllegalArgumentException("Invalid image address: " + args[1]);
        }
        byte[] header = new byte[128];
        readBytes(image, 0, header, header.length, "128-byte PCX header");
        require((header[0] & 255) == 10, "PCX manufacturer must be 10");
        require((header[2] & 255) == 1, "Only PCX RLE encoding 1 is supported");
        require((header[3] & 255) == 8, "Only 8 bits per pixel per plane are supported");
        int width = u16(header, 8) - u16(header, 4) + 1;
        int height = u16(header, 10) - u16(header, 6) + 1;
        int planes = header[65] & 255;
        int bytesPerLine = u16(header, 66);
        require(width > 0 && height > 0, "PCX dimensions must be positive");
        require(planes == 1 || planes == 3, "Only 1-plane or 3-plane PCX is supported");
        require(bytesPerLine >= width, "PCX bytesPerLine must be at least width");
        long expected = (long) height * planes * bytesPerLine;
        require(expected <= MAX_DECODED_BYTES,
            "Decoded PCX size " + expected + " exceeds the 64 MiB limit");

        // Count decoded bytes, including scanline padding; never allocate a decoded bitmap.
        long offset = 128;
        long decoded = 0;
        long nextCancellationCheck = 0;
        while (decoded < expected) {
            if (decoded >= nextCancellationCheck) {
                monitor.checkCancelled();
                nextCancellationCheck = decoded + 65536;
            }
            long tokenOffset = offset;
            int token = readByte(image, offset++, "PCX RLE token");
            int count = 1;
            if ((token & 0xc0) == 0xc0) {
                count = token & 0x3f;
                require(count != 0, "Zero-length PCX RLE run at image offset " + tokenOffset);
                require(count <= expected - decoded,
                    "PCX RLE run at image offset " + tokenOffset + " exceeds decoded size " + expected);
                readByte(image, offset++, "PCX RLE run value");
            }
            decoded += count;
        }
        long rleEndOffset = offset;
        Address rleEnd = addressAt(image, rleEndOffset, "PCX RLE end (exclusive)");
        String palette;
        if (planes == 1) {
            require(readByte(image, offset, "PCX palette marker") == 0x0c,
                "Single-plane PCX without an immediate 0x0c + 768-byte palette is unsupported; no palette is invented");
            byte[] trailer = new byte[769];
            readBytes(image, offset, trailer, trailer.length, "original PCX 256-color palette trailer");
            offset += trailer.length;
            palette = "original 0x0c marker and 768 RGB palette bytes retained";
        } else {
            palette = "none (3-plane RGB)";
        }

        String pointerReport = "";
        if (args.length >= 3) {
            Address pointer = toAddr(args[2]);
            if (pointer == null) {
                throw new IllegalArgumentException("Invalid pointer address: " + args[2]);
            }
            int size = currentProgram.getDefaultPointerSize();
            require(size >= 1 && size <= 8, "Unsupported pointer width: " + size);
            byte[] bytes = new byte[size];
            readBytes(pointer, 0, bytes, size, "image pointer value");
            long value = 0;
            boolean bigEndian = currentProgram.getLanguage().isBigEndian();
            for (int i = 0; i < size; i++) {
                int index = bigEndian ? i : size - 1 - i;
                value = (value << 8) | (bytes[index] & 255L);
            }
            pointerReport = "pointerVA=" + pointer + "\npointerBytes=" + size +
                "\npointerValue=0x" + String.format("%0" + (size * 2) + "x", value) + "\n";
        }

        Path directory = Path.of(args[0]);
        Files.createDirectories(directory);
        String stem = currentProgram.getName().replaceAll("[^A-Za-z0-9._-]", "_") + "." +
            image.toString().replaceAll("[^A-Za-z0-9._-]", "_");
        Path pcx = directory.resolve(stem + ".pcx");
        Path report = directory.resolve(stem + ".pcx.txt");
        Path temporary = Files.createTempFile(directory, stem + ".", ".pcx.tmp");
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        String sha256;
        StringBuilder provenance = new StringBuilder();
        try {
            try (OutputStream output = Files.newOutputStream(temporary)) {
                byte[] buffer = new byte[16384];
                for (long copied = 0; copied < offset;) {
                    monitor.checkCancelled();
                    int length = (int) Math.min(buffer.length, offset - copied);
                    readBytes(image, copied, buffer, length, "validated encoded PCX bytes");
                    output.write(buffer, 0, length);
                    digest.update(buffer, 0, length);
                    copied += length;
                }
            }
            sha256 = HexFormat.of().formatHex(digest.digest());
            for (int i = 3; i < args.length; i++) {
                provenance.append(scanFile(Path.of(args[i]), header, expected, planes, sha256));
            }
            Files.move(temporary, pcx, StandardCopyOption.REPLACE_EXISTING);
        } finally {
            Files.deleteIfExists(temporary);
        }
        String text = "program=" + currentProgram.getName() + "\nimageVA=" + image +
            "\nwidth=" + width + "\nheight=" + height + "\nplanes=" + planes +
            "\nbytesPerLine=" + bytesPerLine + "\ndecodedBytes=" + expected +
            "\nrleStartVA=" + addressAt(image, 128, "PCX RLE start") +
            "\nrleEndVAExclusive=" + rleEnd + "\nrleEncodedLength=" + (rleEndOffset - 128) +
            "\npcxLength=" + offset + "\npalette=" + palette +
            "\nsha256=" + sha256 + "\n" + pointerReport + provenance;
        Files.writeString(report, text, StandardCharsets.UTF_8);
        println("Embedded PCX exported to " + pcx.toAbsolutePath());
        println("Extraction report: " + report.toAbsolutePath());
    }

    private record Candidate(long fileOffset, long rleLength, long payloadLength, String sha256) {}

    private String scanFile(Path file, byte[] header, long expected, int planes, String imageSha)
            throws Exception {
        int headerMatches = 0;
        int validCandidates = 0;
        Candidate candidate = null;
        String lastFailure = "no exact header found";
        // Preserve 127 bytes between chunks so headers crossing chunk boundaries are found once.
        byte[] buffer = new byte[65536 + 127];
        long base = 0;
        int carry = 0;
        try (InputStream input = Files.newInputStream(file)) {
            while (true) {
                monitor.checkCancelled();
                int fresh = input.readNBytes(buffer, carry, 65536);
                if (fresh == 0) {
                    break;
                }
                int length = carry + fresh;
                for (int start = 0; start <= length - header.length; start++) {
                    if (buffer[start] != header[0]) {
                        continue;
                    }
                    int matched = 1;
                    while (matched < header.length && buffer[start + matched] == header[matched]) {
                        matched++;
                    }
                    if (matched != header.length) {
                        continue;
                    }
                    headerMatches++;
                    try {
                        Candidate found = validateFileCandidate(file, base + start, header, expected, planes);
                        validCandidates++;
                        candidate = found;
                    } catch (IOException e) {
                        lastFailure = "candidate at 0x" + Long.toHexString(base + start) + ": " + e.getMessage();
                    }
                }
                carry = Math.min(127, length);
                base += length - carry;
                System.arraycopy(buffer, length - carry, buffer, 0, carry);
            }
        }
        require(validCandidates == 1, "Expected one validated PCX in " + file +
            ", found " + validCandidates + " (exact header matches " + headerMatches +
            "; last rejected candidate: " + lastFailure + ")");
        return "\nsourceFile=" + file.toAbsolutePath() +
            "\nsourceHeaderMatches=" + headerMatches +
            "\nsourceValidCandidateCount=" + validCandidates +
            "\nsourcePhysicalFileOffset=0x" + Long.toHexString(candidate.fileOffset()) +
            "\nsourceRleEncodedLength=" + candidate.rleLength() +
            "\nsourcePayloadLength=" + candidate.payloadLength() +
            "\nsourceSha256=" + candidate.sha256() +
            "\nsourceMatchesExport=" + candidate.sha256().equals(imageSha) + "\n";
    }

    private Candidate validateFileCandidate(Path file, long start, byte[] header, long expected, int planes)
            throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        digest.update(header);
        long encodedLength = 0;
        long decoded = 0;
        long nextCancellationCheck = 0;
        try (InputStream input = new BufferedInputStream(Files.newInputStream(file))) {
            input.skipNBytes(start + header.length);
            while (decoded < expected) {
                if (decoded >= nextCancellationCheck) {
                    monitor.checkCancelled();
                    nextCancellationCheck = decoded + 65536;
                }
                int token = input.read();
                require(token >= 0, "Truncated RLE token after " + encodedLength + " encoded bytes");
                digest.update((byte) token);
                encodedLength++;
                int count = 1;
                if ((token & 0xc0) == 0xc0) {
                    count = token & 0x3f;
                    require(count > 0, "Zero-length RLE run");
                    require(count <= expected - decoded, "RLE run exceeds decoded size " + expected);
                    int value = input.read();
                    require(value >= 0, "Truncated RLE run value");
                    digest.update((byte) value);
                    encodedLength++;
                }
                decoded += count;
            }
            long payloadLength = header.length + encodedLength;
            if (planes == 1) {
                int marker = input.read();
                require(marker == 0x0c, "Missing original 256-color palette marker");
                digest.update((byte) marker);
                for (int i = 0; i < 768; i++) {
                    int color = input.read();
                    require(color >= 0, "Truncated original 256-color palette");
                    digest.update((byte) color);
                }
                payloadLength += 769;
            }
            return new Candidate(start, encodedLength, payloadLength, HexFormat.of().formatHex(digest.digest()));
        }
    }

    private static int u16(byte[] bytes, int offset) {
        return (bytes[offset] & 255) | ((bytes[offset + 1] & 255) << 8);
    }

    private static void require(boolean valid, String message) throws IOException {
        if (!valid) {
            throw new IOException(message);
        }
    }

    private Address addressAt(Address base, long offset, String purpose) throws IOException {
        try {
            return base.addNoWrap(offset);
        } catch (AddressOverflowException e) {
            throw new IOException("Address overflow reading " + purpose + " at " + base +
                " + " + offset, e);
        }
    }

    private int readByte(Address base, long offset, String purpose) throws IOException {
        Address address = addressAt(base, offset, purpose);
        try {
            return currentProgram.getMemory().getByte(address) & 255;
        } catch (MemoryAccessException e) {
            throw new IOException("Missing/uninitialized memory reading " + purpose + " at " +
                address + " (image offset " + offset + ")", e);
        }
    }

    private void readBytes(Address base, long offset, byte[] buffer, int length, String purpose)
            throws IOException {
        Address address = addressAt(base, offset, purpose);
        addressAt(base, offset + length - 1L, purpose);
        try {
            int actual = currentProgram.getMemory().getBytes(address, buffer, 0, length);
            require(actual == length, "Truncated " + purpose + " at " + address +
                ": expected " + length + " bytes, read " + actual);
        } catch (MemoryAccessException e) {
            throw new IOException("Missing/uninitialized memory reading " + purpose + " at " +
                address + " (offset " + offset + ", length " + length + ")", e);
        }
    }
}
