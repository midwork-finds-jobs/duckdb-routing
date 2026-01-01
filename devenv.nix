{
  pkgs,
  lib,
  config,
  inputs,
  ...
}:

{
  enterShell = ''
    export GEN=ninja
    # VCPKG disabled - using system packages instead
    # export VCPKG_TOOLCHAIN_PATH=${pkgs.vcpkg}/share/vcpkg/scripts/buildsystems/vcpkg.cmake
  '';

  # https://devenv.sh/packages/
  packages = with pkgs; [
    git
    gnumake

    # For faster compilation
    ninja

    # C/C++ tools
    autoconf
    automake
    pkg-config

    # For this extension specifically
    openssl
    curl

    # Valhalla dependencies (Valhalla itself via vcpkg)
    boost
    protobuf
    sqlite
    lua
    rapidjson
  ];

  # Rust toolchain for the routing library
  languages.rust.enable = true;

  # https://devenv.sh/languages/
  languages.cplusplus.enable = true;

  git-hooks.hooks = {
    # Nix files
    nixfmt-rfc-style.enable = true;
    # Github Actions
    actionlint.enable = true;
    # Markdown files
    markdownlint = {
      enable = true;
      settings.configuration = {
        # Ignore line lengths for now
        MD013 = false;
        # Allow inline html as it is used in phoenix default AGENTS.md
        MD033 = false;
      };
    };
    # Leaking secrets
    ripsecrets.enable = true;
  };
  # Prevents unencrypted sops files from being committed
  git-hooks.hooks.pre-commit-hook-ensure-sops = {
    enable = true;
    files = "secret.*\\.(env|ini|yaml|yml|json)$";
  };

  # Security hardening to prevent malicious takeover of Github Actions:
  # https://news.ycombinator.com/item?id=43367987
  # Replaces tags like "v4" in 3rd party Github Actions to the commit hashes
  git-hooks.hooks.lock-github-action-tags = {
    enable = true;
    files = "^.github/workflows/";
    types = [ "yaml" ];
    entry =
      let
        script_path = pkgs.writeShellScript "lock-github-action-tags" ''
          for workflow in "$@"; do
            grep -E "uses:[[:space:]]+[A-Za-z0-9._-]+/[A-Za-z0-9._-]+@v[0-9]+" "$workflow" | while read -r line; do
              repo=$(echo "$line" | sed -E 's/.*uses:[[:space:]]+([A-Za-z0-9._-]+\/[A-Za-z0-9._-]+)@v[0-9]+.*/\1/')
              tag=$(echo "$line" | sed -E 's/.*@((v[0-9]+)).*/\1/')
              commit_hash=$(git ls-remote "https://github.com/$repo.git" "refs/tags/$tag" | cut -f1)
              [ -n "$commit_hash" ] && sed -i.bak -E "s|(uses:[[:space:]]+$repo@)$tag|\1$commit_hash #$tag|g" "$workflow" && rm -f "$workflow.bak"
            done
          done
        '';
      in
      toString script_path;
  };

  # Run clang-tidy and clang-format on commits
  git-hooks.hooks = {
    clang-format = {
      enable = true;
      types_or = [
        "c++"
        "c"
      ];
    };
    clang-tidy = {
      enable = false;
      types_or = [
        "c++"
        "c"
      ];
      entry = "clang-tidy -p build --fix";
    };
  };

  # Verify description.yml ref exists in GitHub
  git-hooks.hooks.verify-description-yml-ref = {
    enable = true;
    files = "description\\.yml$";
    entry =
      let
        script_path = pkgs.writeShellScript "verify-description-yml-ref" ''
          set -e
          for file in "$@"; do
            if [[ ! -f "$file" ]]; then
              continue
            fi

            # Extract repo.github and repo.ref using grep/sed (no yaml parser needed)
            repo_line=$(grep -A 2 "^repo:" "$file" | grep "github:" | head -1)
            ref_line=$(grep -A 2 "^repo:" "$file" | grep "ref:" | head -1)

            if [[ -z "$repo_line" || -z "$ref_line" ]]; then
              echo "Warning: Could not find repo.github or repo.ref in $file"
              continue
            fi

            # Extract values (trim whitespace and quotes)
            repo=$(echo "$repo_line" | sed -E 's/.*github:[[:space:]]*([^[:space:]]+).*/\1/' | tr -d '"')
            ref=$(echo "$ref_line" | sed -E 's/.*ref:[[:space:]]*([^[:space:]]+).*/\1/' | tr -d '"')

            if [[ -z "$repo" || -z "$ref" ]]; then
              echo "Error: Empty repo or ref in $file"
              exit 1
            fi

            echo "Verifying commit $ref exists in https://github.com/$repo"

            # Check if commit exists using git ls-remote
            if ! git ls-remote "https://github.com/$repo.git" "$ref" | grep -q "$ref"; then
              # Try as a commit hash
              if ! git ls-remote "https://github.com/$repo.git" | grep -q "^$ref"; then
                echo "Error: Commit $ref does not exist in https://github.com/$repo"
                echo "Please verify the ref in $file points to a valid commit"
                exit 1
              fi
            fi

            echo "✓ Commit $ref verified in $repo"
          done
        '';
      in
      toString script_path;
  };
}
