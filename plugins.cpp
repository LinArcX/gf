#include <ctype.h>
#include <string.h>

//////////////////////////////////////////////////////
// NM window:
//////////////////////////////////////////////////////

struct NMResult {
	char *symbol;
	char *line;
};

struct Match {
	NMResult *result;
	int score;
};

struct NMWindow {
	bool loaded;
	UICode *display;
	UITextbox *textbox;
	Array<NMResult> results;
};

const char* GetCurrentExecutablePath() {
  // Ask GDB for the executable path
  EvaluateCommand("info proc", false);

  // Parse the result:
  // cmdline = '/full/path/to/binary'
  const char* result = evaluateResult;
  const char* start = strstr(result, "cmdline = '");
  
  if (start) {
    start += 11;  // skip "cmdline = '"
    const char* end = strchr(start, '\'');
    if (end) {
      static char path[PATH_MAX];
      StringFormat(path, sizeof(path), "%.*s", (int)(end - start), start);
      return path;
    }
  }
  return nullptr;
}

static void LoadNMResults(NMWindow* window)
{
	if (window->loaded) {
		return;
	}
	const char *path = GetCurrentExecutablePath();
	if (!path || !*path) {
		return;
	}

	char command[PATH_MAX + 32];
	StringFormat(command, sizeof(command), "shell nm %s", path);
	EvaluateCommand(command);
	char* position = evaluateResult;

	while (*position) {
		char* next = strchr(position, '\n');
		size_t lineLength = next ? (size_t) (next - position) : strlen(position);
		if (!lineLength) {
			break;
		}

		char* line = (char*) calloc(1, lineLength + 1);
		memcpy(line, position, lineLength);

		char* symbol = strrchr(line, ' ');
		if (symbol) {
			symbol++;
		} else {
			symbol = line;
		}

		NMResult result = {};
		result.line = line;
		result.symbol = symbol;

		window->results.Add(result);

		if (!next) {
			break;
		}
		position = next + 1;
	}
	window->loaded = true;
}

int TextboxSearchNM(UIElement *element, UIMessage message, int di, void *dp)
{
	if (message != UI_MSG_KEY_TYPED) {
	  return 0;
	}
	else {
	  NMWindow* window = (NMWindow *) element->cp;
	  LoadNMResults(window);

		char query[4096];
		char buffer[4096];
		bool firstMatch = true;
		StringFormat(query, sizeof(query), "%.*s", (int) window->textbox->bytes, window->textbox->string);

		for (int i = 0; i < window->results.Length(); i++) {
			if (strstr(window->results[i].symbol, query)) {
				StringFormat(buffer, sizeof(buffer), "%s", window->results[i].line);
				UICodeInsertContent(window->display, buffer, -1, firstMatch);
				firstMatch = false;
			}
		}

		if (firstMatch) {
			UICodeInsertContent(window->display, "(no matches)", -1, firstMatch);
		}

		window->display->vScroll->position = 0;
		UIElementRefresh(&window->display->e);
	}
	return 0;
}

UIElement *NMWindowCreate(UIElement *parent) {
	NMWindow *window = (NMWindow*) calloc(1, sizeof(NMWindow));
	UIPanel *panel = UIPanelCreate(parent, UI_PANEL_COLOR_1 | UI_PANEL_EXPAND);

	window->textbox = UITextboxCreate(&panel->e, 0);
	window->textbox->e.messageUser = TextboxSearchNM;
	window->textbox->e.cp = window;
	window->display = UICodeCreate(&panel->e, UI_ELEMENT_V_FILL | UI_CODE_NO_MARGIN | UI_CODE_SELECTABLE);
	window->loaded = false;
	window->results = {0};

	UICodeInsertContent(window->display, "Type here to search in NM output.", -1, true);

	return &panel->e;
}

//////////////////////////////////////////////////////
// ASM window:
//////////////////////////////////////////////////////

UIElement* ASMWindowCreate(UIElement *parent) 
{
  UICode*	uiAsmCode = UICodeCreate(parent, selectableSource ? UI_CODE_SELECTABLE : 0);
  uiAsmCode->centerExecutionPointer = true;
	return &uiAsmCode->e;
}

void ASMWindowUpdate(const char *data, UIElement *element)
{
  EvaluateCommand("disassemble /m $pc-128,$pc+128");  // Wider range for context

  if (strstr(evaluateResult, "No registers.") || 
    strstr(evaluateResult, "The current thread has terminated")) {
    return;
  }
  else {
	  UICode* asmCode = (UICode *) element;
	  UICodeInsertContent(asmCode, evaluateResult, -1, true);

    char *current = strstr(evaluateResult, "=>");

    if (current) {
      int line = 0;
	    int lineHeight = UIMeasureStringHeight();
		  int viewHeight = UI_RECT_HEIGHT(asmCode->e.bounds);
      for (char *p = evaluateResult; p < current; p++) {
        if (*p == '\n') line++;
      }
      asmCode->vScroll->position = line * lineHeight - (viewHeight / 2);
    }
  }
  UIElementRefresh(element);
}

__attribute__((constructor))
void ASMPluginRegister() {
    interfaceWindows.Add({ "ASM", ASMWindowCreate, ASMWindowUpdate });
    interfaceWindows.Add({ "NM", NMWindowCreate, nullptr });
}
