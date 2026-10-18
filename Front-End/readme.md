# Eco-Nexus Frontend

The frontend of **Eco-Nexus**, a carbon compliance and simulated carbon trading platform designed to support sustainability tracking, carbon credit management, and trading-related operations.

## Overview

The frontend provides the user interface for interacting with the Eco-Nexus platform. It is built using HTML, CSS, and JavaScript, with Tailwind CSS for styling.

## Tech Stack

- **HTML5** – Structure of the web pages
- **CSS3** – Styling and responsive layouts
- **JavaScript** – Client-side interactivity
- **Tailwind CSS** – Utility-first styling framework

## Project Structure

```text
frontend/
├── index.html
├── package.json
├── package-lock.json
└── ... (other frontend assets and files)
```

## Getting Started

### Prerequisites

- A modern web browser
- Visual Studio Code (recommended)
- Node.js and npm (required for CSS build commands)
- Live Server extension for VS Code (optional, for local preview)

### Installation

1. Clone the repository:

   ```bash
   git clone <repository-url>
   ```

2. Navigate to the frontend directory:

   ```bash
   cd frontend
   ```

3. Install dependencies:

   ```bash
   npm install
   ```

### Running the Frontend

To preview the interface, open `index.html` using the Live Server extension in VS Code.

Alternatively, if the full application is configured, run the frontend through the Eco-Nexus backend server as described in the main project README.

### CSS Development

Build the CSS:

```bash
npm run build:css
```

Watch for CSS changes during development:

```bash
npm run watch:css
```

## Integration

The frontend is part of the larger Eco-Nexus platform and is designed to work alongside its backend services and database components.

Some features may require the backend server and database to be configured and running.

## Contributing

1. Create a new branch for your changes.
2. Make and test your changes locally.
3. Commit your changes with a descriptive message.
4. Submit a pull request for review.

## License

Refer to the root repository's README for licensing information.
