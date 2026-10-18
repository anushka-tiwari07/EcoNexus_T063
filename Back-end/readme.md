# Eco-Nexus Backend

The backend of **Eco-Nexus**, a carbon compliance and simulated carbon trading platform focused on carbon credit management, sustainability tracking, and trading operations.

## Overview

The backend handles server-side operations and provides APIs to support communication between the frontend, database, and core application logic.

It is developed in C++ and is designed to support authentication, carbon trading operations, and carbon compliance functionality.

## Tech Stack

- **C++** – Backend logic and server-side processing
- **CMake** – Build system
- **PostgreSQL** – Relational database
- **REST APIs** – Communication between the frontend and backend
- **GCC** – C++ compilation

## Project Structure

```text
backend/
├── include/       # Header files
├── src/           # C++ source files
├── CMakeLists.txt # Build configuration
├── .env.example   # Example environment configuration, if provided
└── ...            # Additional backend files
```

## Prerequisites

- GCC 13 or newer
- CMake 3.25 or newer
- PostgreSQL 14 or newer
- Required C++ dependencies listed in the project configuration

## Getting Started

### 1. Clone the Repository

```bash
git clone <repository-url>
cd eco-nexus-platform/backend
```

### 2. Configure the Environment

Create a `.env` file using the provided example configuration, if available.

Configure the required database connection details and other environment variables.

**Important:** Never commit passwords, database credentials, API keys, or other secrets to GitHub.

### 3. Set Up the Database

Install and configure PostgreSQL, create the required database, and apply the database migrations provided in the project's `db/` directory. Refer to the root README for the exact database setup instructions.

### 4. Build the Backend

From the backend directory, run:

```bash
cmake -S . -B build
cmake --build build
```

These commands configure and compile the project, provided the required dependencies and build configuration are available.

### 5. Run the Server

Run the generated server executable using the instructions provided in the root README or the backend configuration.

Ensure the database is running and the environment variables are configured before starting the server.

## Integration

The backend is designed to work with the Eco-Nexus frontend and PostgreSQL database.

It provides the server-side foundation for application features such as authentication, carbon credit management, and simulated trading operations.

## Development Notes

- Keep business logic separate from HTTP handling where possible.
- Validate incoming requests and handle errors appropriately.
- Use environment variables for configuration and sensitive credentials.
- Test API endpoints and database operations before integration.

## Contributing

1. Create a separate branch for your changes.
2. Implement and test your changes.
3. Commit with a descriptive message.
4. Submit a pull request for review.

## License

Refer to the root repository's README for licensing information.
