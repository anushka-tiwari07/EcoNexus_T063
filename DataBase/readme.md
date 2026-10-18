# Eco-Nexus Database

The database component of **Eco-Nexus**, a carbon compliance and simulated carbon trading platform. It manages the structured data required to support carbon credit operations, compliance tracking, and trading-related functionality.

## Overview

The `db` directory contains database migration files used to create and update the database schema as the project evolves.

The platform uses PostgreSQL as its relational database management system.

## Tech Stack

- **PostgreSQL** – Relational database management system
- **SQL** – Database schema definitions and queries
- **Database Migrations** – Version-controlled database schema changes

## Project Structure

```text
db/
├── migrations/    # Database migration scripts
└── README.md      # Database documentation
```

*Note: The directory structure above is illustrative. Adjust it to match the actual files in this folder.*

## Prerequisites

- PostgreSQL 14 or newer
- A PostgreSQL client, such as `psql`
- The database migration files provided in this directory

## Getting Started

### 1. Create the Database

Open the PostgreSQL command-line interface and create a database for Eco-Nexus:

```sql
CREATE DATABASE eco_nexus;
```

### 2. Connect to the Database

```bash
psql -U postgres -d eco_nexus
```

Replace `postgres` with your PostgreSQL username if necessary.

### 3. Apply Database Migrations

Run the migration scripts in the order specified by the project documentation.

For example, if a migration is a standalone SQL file:

```bash
psql -U postgres -d eco_nexus -f path/to/migration.sql
```

Replace `path/to/migration.sql` with the actual migration file path.

If the project uses a migration tool, follow its configuration and commands instead.

## Integration

The database is designed to work with the Eco-Nexus backend, which handles database connections and application operations.

It provides persistent storage for the information required by the platform's supported features.

## Development Guidelines

- Keep database schema changes in version-controlled migration files.
- Use appropriate primary keys, foreign keys, and constraints.
- Validate schema changes before integrating them with the backend.
- Store database credentials in environment variables rather than source code.
- Never commit passwords or other sensitive credentials to GitHub.

## Contributing

1. Create a branch for your changes.
2. Add or update migration files as needed.
3. Test migrations against a development database.
4. Submit a pull request with a description of the changes.

## License

Refer to the root repository's README for licensing information.
