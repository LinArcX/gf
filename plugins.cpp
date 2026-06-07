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

int ASMRowMessage(UIElement *element, UIMessage message, int di, void *dp)
{
  if (message == UI_MSG_PAINT) {
    char *text = (char *)element->cp;
    if (!text) return 0;

    UIPainter *painter = (UIPainter *)dp;
    UIRectangle bounds = element->bounds;

    // Choose color
    uint32_t color = ui.theme.text; // default

    if (strstr(text, "=>")) {
      color = 0xFF66CC66; // Green for current instruction
    } else if (strstr(text, "0x")) {
      color = 0xFFAAAAAA; // Gray for assembly instructions
    } else {
      color = 0xFFFFFFFF; // White for c instructions
    }

    // Draw the text (this overrides the child UILabel's default color)
    UIDrawString(painter, bounds, text, -1, color, UI_ALIGN_LEFT, NULL);

    return 1; // Handled painting
  } 
  else if (message == UI_MSG_DEALLOCATE) {
    if (element->cp) {
      UI_FREE(element->cp);
      element->cp = NULL;
    }
  }
  return 0;
}

UIElement* ASMWindowCreate(UIElement *parent) 
{
  UIPanel *panel = UIPanelCreate(parent, UI_PANEL_SMALL_SPACING | UI_PANEL_COLOR_1 | UI_PANEL_SCROLL);
  return &panel->e;
}

void ASMWindowUpdate(const char *data, UIElement *panel)
{
  EvaluateCommand("disassemble /m $pc-128,$pc+128");  // Wider range for context

  if (strstr(evaluateResult, "No registers.") || 
    strstr(evaluateResult, "The current thread has terminated")) {
    return;
  }

  UIElementDestroyDescendents(panel);
  int currentRowIndex = -1;
  int rowIndex = 0;

  char *line = strtok(evaluateResult, "\n");
  while (line != NULL) {
    // Skip empty lines or GDB noise
    while (isspace(*line)) line++;
    if (*line == '\0' || strstr(line, "Dump of assembler code") || strstr(line, "End of assembler dump")) {
      line = strtok(NULL, "\n");
      continue;
    }

    UIPanel *row = UIPanelCreate(panel, UI_PANEL_HORIZONTAL | UI_ELEMENT_H_FILL | UI_PANEL_SMALL_SPACING);

    // Attach custom painter
    row->e.flags |= UI_ELEMENT_H_FILL;
    row->e.bounds.b = row->e.bounds.t + 20;
    row->e.messageUser = ASMRowMessage;

    // Copy line for the message handler (freed on deallocate)
    row->e.cp = UIStringCopy(line, -1);

    // Create the visible label (it will be overridden by our paint handler)
    UILabelCreate(&row->e, 0, " ", -1);

    if (strstr(line, "=>") && currentRowIndex == -1) {
      currentRowIndex = rowIndex;
    }
    rowIndex++;
    line = strtok(NULL, "\n");
  }

  // Auto-scroll to the current instruction line
  if (currentRowIndex >= 0 && panel->children && panel->childCount > 0) {
    UIPanel *scrollPanel = (UIPanel *)panel;
    if (scrollPanel->scrollBar) {
      int rowHeight = 20 * panel->window->scale; // Approximate row height (adjust if needed)
      int rowY = currentRowIndex * rowHeight;
      int visibleHeight = UI_RECT_HEIGHT(panel->bounds) - 40 * panel->window->scale;
      int targetScroll = rowY + visibleHeight / 3; //+ rowHeight / 2;
      if (targetScroll < 0) targetScroll = 0;
      // Center the line roughly
      scrollPanel->scrollBar->position = targetScroll;
    }
  }
  UIElementRefresh(panel);
}

__attribute__((constructor))
void ASMPluginRegister() {
    interfaceWindows.Add({ "ASM", ASMWindowCreate, ASMWindowUpdate });
    interfaceWindows.Add({ "NM", NMWindowCreate, nullptr });
}
