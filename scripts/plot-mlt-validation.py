"""Plot actual MLT validation data; matplotlib is an optional validation tool."""
import json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
source=Path('build/validation/mlt-web.json')
records=json.loads(source.read_text())
output=Path('docs/public/benchmarks')
output.mkdir(parents=True,exist_ok=True)
glass=[r for r in records if r['case']=='glass']
audits=[{'seed':1, 'stats':glass[-1]['stats']}] + [r for r in records if r['case']=='bootstrap-audit']
reference=glass[-1]['refAll']-glass[-1]['directMean']
fig,axes=plt.subplots(1,2,figsize=(9.4,3.6),layout='constrained')
axes[0].loglog([r['mpp'] for r in glass],[r['mse'] for r in glass],'o-',color='#805acb')
axes[0].set(xlabel='Mutations / pixel',ylabel='ROI linear luminance MSE',title='Glass caustics: PSSMLT convergence')
axes[0].grid(True,which='both',alpha=.2)
axes[1].errorbar([r['seed'] for r in audits],[r['stats']['normalization'] for r in audits],yerr=[r['stats']['normalizationSE'] for r in audits],fmt='o',capsize=5,color='#805acb',label='Bootstrap mean ± 1 SE')
axes[1].axhline(reference,color='#555555',ls='--',label='BDPT minus direct-light audit')
axes[1].set(xlabel='Independent bootstrap seed',ylabel='Indirect luminance normalization',title='262144 bootstrap samples / seed',xticks=[1,2,3],xlim=(.5,3.5))
axes[1].grid(True,alpha=.2);axes[1].legend(fontsize=8)
fig.suptitle('32×32 · 8 bounces · 8192-spp BDPT reference · no denoising',fontsize=11)
fig.savefig(output/'mlt-validation.png',dpi=160)
plt.close(fig)
export=[]
for record in records:
    if record['case']=='native-parity':
        continue
    if record['case']=='bootstrap-cdf-indices' and any(r['case']==record['case'] for r in export):
        continue
    export.append(record)
(output/'mlt-validation.json').write_text(json.dumps(export,indent=2))
print(output/'mlt-validation.png')
